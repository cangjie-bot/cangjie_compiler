// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Diags.h"
#include "NodeContext.h"
#include "ScopeManager.h"
#include "TypeCheckerImpl.h"
#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {
using namespace AST;

static FuncBody* GetFuncBody(const Node& funcLike)
{
    if (auto fd = DynamicCast<FuncDecl>(&funcLike)) {
        return fd->funcBody.get();
    }
    if (auto le = DynamicCast<LambdaExpr>(&funcLike)) {
        return le->funcBody.get();
    }
    if (auto md = DynamicCast<MacroDecl>(&funcLike)) {
        return md->desugarDecl->funcBody.get();
    }
    if (Is<PrimaryCtorDecl>(&funcLike)) {
        // desugared, should not have body
        return nullptr;
    }
    return StaticCast<MainDecl>(&funcLike)->funcBody.get();
}

/// Track function context with default param value state
struct FuncContext {
    Node* func = nullptr;
    bool inDefaultParamValue = false;
};

/// All checks:
/// 1. Check return expression of a function body cannot have internal @local! type
/// 2. Check global var cannot have local type
/// 3. Check call expr args cannot be external @local! type if the param is @local! nor Copy
/// 4. Check validity of @MakeCopy
/// 5. Check local of captured variables
/// 6. Check manually implements Copy
/// 7. Check assignment/member-assignment
/// 8. Check exclave expr is inside a function, or global or static var initializer (not counting default param values
/// as inside that function)
/// 9. Check exclave expr is not in constructor, finalizer, main, or spawn expr
/// 10. Check member var local type validity
struct ModalTypeChecker {
    ModalTypeChecker(DiagnosticEngine& diag, TypeManager& m) : d(diag), type{m}
    {
    }

    void Check(const ASTContext& ctx, Package& pkg)
    {
        // Clear state before walking
        funcStack.clear();
        currentSpawnExpr = nullptr;
        currentGlobalStaticVar = nullptr;

        Walker walker(&pkg, [this, &ctx](Node* node) -> VisitAction {
            if (node->astKind == ASTKind::PACKAGE || node->astKind == ASTKind::FILE) {
                return VisitAction::WALK_CHILDREN;
            }
            if (node->TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
                return VisitAction::SKIP_CHILDREN;
            }
            if (auto decl = DynamicCast<InheritableDecl>(node)) {
                CheckImplementsCopy(*decl);
            }
            if (auto decl = DynamicCast<StructDecl>(node)) {
                if (decl->IsCopyType()) {
                    CheckCopyType(*decl);
                }
                CheckMemberVarModality(*decl);
            }
            if (auto classDecl = DynamicCast<ClassDecl>(node)) {
                CheckMemberVarModality(*classDecl);
            }
            if (auto var = DynamicCast<VarDecl>(node)) {
                if (!Ty::IsTyCorrect(var->ty)) {
                    return VisitAction::SKIP_CHILDREN;
                }
                CheckGlobalVarModalType(*var);
            }
            if (auto call = DynamicCast<CallExpr>(node)) {
                if (!Ty::IsTyCorrect(call->ty)) {
                    return VisitAction::SKIP_CHILDREN;
                }
                CheckCallExpr(ctx, *call);
            }
            if (auto assign = DynamicCast<AssignExpr>(node); assign && assign->TestAttr(Attribute::LEFT_VALUE)) {
                CheckAssignExpr(ctx, *assign);
            }
            if (node->IsFuncLike()) {
                if (!Ty::IsTyCorrect(node->ty)) {
                    return VisitAction::SKIP_CHILDREN;
                }
                CheckReturnModalType(ctx, *node);
                if (Is<FuncDecl>(node) || Is<LambdaExpr>(node)) {
                    CheckCaptures(ctx, *node);
                }
                // Push function context
                funcStack.push_back({node, false});
            }
            // Track entering default param value
            if (auto fp = DynamicCast<FuncParam>(node); fp && fp->assignment && !funcStack.empty()) {
                funcStack.back().inDefaultParamValue = true;
            }
            // Track entering spawn block (only if not already inside one)
            if (Is<SpawnExpr>(node) && currentSpawnExpr == nullptr) {
                currentSpawnExpr = node;
            }
            // Track entering global/static var initializer (only if not already inside one)
            if (auto var = DynamicCast<VarDecl>(node);
                var && var->initializer && IsGlobalOrMember(*var) && currentGlobalStaticVar == nullptr) {
                currentGlobalStaticVar = node;
            }
            // Check exclave expr
            if (auto exclave = DynamicCast<ExclaveExpr>(node)) {
                CheckExclaveInsideFunction(*exclave);
                CheckExclaveInCtor(ctx, *exclave);
            }
            if (auto lambda = DynamicCast<LambdaExpr>(node)) {
                CheckNeedsRegion(*lambda);
            }
            if (auto func = DynamicCast<FuncDecl>(node)) {
                CheckNeedsRegion(*func);
            }
            return VisitAction::WALK_CHILDREN;
        },
        [this](Node* node) -> VisitAction {
            // Pop function context when exiting
            if (node->IsFuncLike() && !funcStack.empty() && funcStack.back().func == node) {
                funcStack.pop_back();
            }
            // Track exiting default param value
            if (auto fp = DynamicCast<FuncParam>(node); fp && fp->assignment && !funcStack.empty()) {
                funcStack.back().inDefaultParamValue = false;
            }
            // Track exiting spawn block
            if (node == currentSpawnExpr) {
                currentSpawnExpr = nullptr;
            }
            // Track exiting global/static var initializer
            if (node == currentGlobalStaticVar) {
                currentGlobalStaticVar = nullptr;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
    }

    /// 1. external local! used in exclave expr is internal
    /// 2. internal local! used in returned expr of inside exclave expr is external
    /// 3. member access of external local! is external
    /// 4. reference to param of `T` local! is external local!
    /// 5. all other cases are internal
    bool IsExternalLocal(const ASTContext& ctx, const Expr& expr)
    {
        if (IsInExclaveExpr(ctx, expr)) {
            // rule 2
            return IsReturnedExpr(ctx, expr);
        }
        if (auto ref = DynamicCast<RefExpr>(&expr)) {
            if (auto param = DynamicCast<FuncParam>(ref->ref.target)) {
                auto node = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, expr.scopeName);
                if (!node || !node->node) {
                    return false;
                }
                auto func = DynamicCast<FuncDecl>(node->node);
                if (!func) {
                    return false;
                }
                for (auto& fp : func->funcBody->paramLists[0]->params) {
                    if (fp.get() == param) {
                        // rule 4
                        return true;
                    }
                }
            }
        }
        if (auto ma = DynamicCast<MemberAccess>(&expr)) {
            // rule 3
            return IsExternalLocal(ctx, *ma->baseExpr);
        }
        return false;
    }

private:
    /// Check return expression of a function body cannot have internal @local! type
    void CheckReturnModalType(const ASTContext& ctx, const Node& node)
    {
        auto body = GetFuncBody(node);
        if (!body || !body->retType || !Ty::IsTyCorrect(body->retType->ty)) {
            return;
        }
        if (body->retType->ty->modal.local == LocalModal::FULL) {
            auto r = CollectReturnedExpr(*body);
            for (auto& e : r) {
                if (!IsExternalLocal(ctx, *e) && type.NeverImplementsCopyInterface(e->ty)) {
                    DiagBadInternalLocalReturn(*e);
                }
            }
        }
    }

    /// Check that exclave is inside a function body (not counting default param values as inside that function), global
    /// or static var initializer
    void CheckExclaveInsideFunction(const ExclaveExpr& expr)
    {
        // Exclave is allowed inside global/static var initializer
        if (currentGlobalStaticVar != nullptr) {
            return;
        }
        // Find the first function where we're not in its default param value
        Node* enclosingFunc = nullptr;
        for (auto it = funcStack.rbegin(); it != funcStack.rend(); ++it) {
            if (!it->inDefaultParamValue) {
                enclosingFunc = it->func;
                break;
            }
        }
        if (!enclosingFunc) {
            DiagExclaveOutsideFunc(expr);
        }
    }

    /// Check exclave expr is not in constructor, finalizer, main, or spawn expr
    void CheckExclaveInCtor(const ASTContext& ctx, const ExclaveExpr& expr)
    {
        // check whether in spawn
        if (currentSpawnExpr != nullptr) {
            DiagExclaveInCtor(expr, *currentSpawnExpr);
            return;
        }
        // check whether in constructor, main, or finalizer
        if (auto ctorSym = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(ctx, expr.scopeName, [](Symbol& sym) {
            if (auto func = DynamicCast<FuncDecl>(sym.node)) {
                return func->TestAnyAttr(Attribute::MAIN_ENTRY, Attribute::CONSTRUCTOR, Attribute::PRIMARY_CONSTRUCTOR) || func->IsFinalizer();
            };
            return false;
        })) {
            DiagExclaveInCtor(expr, *ctorSym->node);
        }
    }

    void DiagExclaveInCtor(const ExclaveExpr& expr, const Node& node)
    {
        auto name = ASTKIND_TO_STR.at(node.astKind);
        if (auto func = DynamicCast<FuncDecl>(&node)) {
            if (func->IsFinalizer()) {
                name = "finalizer";
            } else if (func->TestAttr(Attribute::MAIN_ENTRY)) {
                name = "main";
            } else {
                name = "constructor";
            }
        }
        d.DiagnoseRefactor(DiagKindRefactor::sema_exclave_in_ctor, expr, name);
    }

    void DiagExclaveOutsideFunc(const ExclaveExpr& expr)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_exclave_outside_function, expr);
    }

    /// The following functions need mark needsRegion:
    /// The function has in its body a func call that returns a non copy non @~local type (including constructor call
    /// and enum constructor call)
    void CheckNeedsRegion(const Node& body, bool& needsRegion)
    {
        if (!Ty::IsTyCorrect(body.ty)) {
            return;
        }
        ConstWalker w{&body, [this, &needsRegion](Ptr<const Node> node) {
            if (auto call = DynamicCast<CallExpr>(node)) {
                if (!call->baseFunc || !Ty::IsTyCorrect(call->ty)) {
                    // invalid call node, skip
                    return VisitAction::SKIP_CHILDREN;
                }
                Ptr<Ty> targetTy = nullptr;
                if (call->callKind == CallKind::CALL_FUNCTION_PTR) {
                    // fp call, no target
                    targetTy = StaticCast<FuncTy>(call->baseFunc->ty);
                } else if (call->callKind == CallKind::CALL_OBJECT_CREATION ||
                    call->callKind == CallKind::CALL_STRUCT_CREATION) {
                    targetTy = call->ty; // do not use retTy because only targetTy has the correct modal type, retTy is
                                         // of data type
                } else {
                    targetTy = call->baseFunc->GetTarget()->ty;
                }
                CJC_NULLPTR_CHECK(targetTy);
                if (auto funcTy = DynamicCast<FuncTy>(targetTy)) {
                    auto retTy = funcTy->retTy;
                    // non copy non @~local type, needs a region
                    if (retTy->modal.local != LocalModal::NOT && type.NeverImplementsCopyInterface(retTy)) {
                        needsRegion = true;
                        return VisitAction::STOP_NOW;
                    }
                }
                if (!type.ImplementsCopyInterface(targetTy) && targetTy->modal.local != LocalModal::NOT) { // enum constructor, primitive types
                    needsRegion = true;
                    return VisitAction::STOP_NOW;
                }
            }
            if (Is<FuncDecl>(node) || Is<LambdaExpr>(node)) {
                // skip nested func
                return VisitAction::SKIP_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        }};
        w.Walk();
    }

    void CheckNeedsRegion(LambdaExpr& lambda)
    {
        CheckNeedsRegion(*lambda.funcBody->body, lambda.needsRegion);
    }

    void CheckNeedsRegion(FuncDecl& func)
    {
        // constructor uses its caller's region implicitly
        if (func.TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::PRIMARY_CONSTRUCTOR)) {
            return;
        }
        if (!func.funcBody->body) {
            return;
        }
        CheckNeedsRegion(*func.funcBody->body, func.needsRegion);
    }

    void CheckAssignExpr(const ASTContext& ctx, const AssignExpr& assign)
    {
        auto right = assign.rightExpr.get();
        if (type.ImplementsCopyInterface(right->ty)) {
            return;
        }
        auto rLocal = right->ty->modal.local;
        auto left = assign.leftValue.get();
        if (!left || !Ty::IsTyCorrect(left->ty)) {
            return;
        }
        auto lLocal = left->ty->modal.local;
        if (auto ma = DynamicCast<MemberAccess>(left)) {
            if (auto ref = DynamicCast<RefExpr>(ma->baseExpr.get()); ref && ref->isThis) {
                auto cons = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(ctx, assign.scopeName, [](Symbol& sym) {
                    if (auto func = DynamicCast<FuncDecl>(sym.node)) {
                        return func->TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::PRIMARY_CONSTRUCTOR) ||
                            func->IsFinalizer();
                    }
                    return false;
                });
                if (cons) {
                    // do not check this member-assignment in constructor or finalizer
                    return;
                }
            }
            lLocal =
                ma->GetTarget()->ty->modal.local == LocalModal::NOT ? ma->baseExpr->ty->modal.local : LocalModal::NOT;
        }
        if (Is<MemberAccess>(left)) {
            if (lLocal == LocalModal::HALF) {
                DiagBadAssignment(*left, "", left->ty->modal.ToString());
            }
            if (lLocal == LocalModal::FULL && rLocal == LocalModal::FULL) {
                auto lext = IsExternalLocal(ctx, *left);
                auto rext = IsExternalLocal(ctx, *right);
                if (lext != rext) {
                    DiagBadAssignment(assign, lext ? " external @local!" : " internal @local!",
                        (rext ? " external" : " internal") + right->ty->String());
                }
            }
            return;
        }
        if (IsCapture(ctx, *left)) {
            if (lLocal != LocalModal::FULL && rLocal == LocalModal::NOT) {
                return;
            }
            DiagBadAssignment(assign, std::string{ToString(lLocal)}, right->ty->String());
        } else {
            if (lLocal == LocalModal::HALF) {
                return;
            }
            if (lLocal == LocalModal::NOT && rLocal == LocalModal::NOT) {
                return;
            }
            if (lLocal == LocalModal::FULL && rLocal == LocalModal::FULL) {
                auto lext = IsExternalLocal(ctx, *left);
                auto rext = IsExternalLocal(ctx, *right);
                if (lext != rext) {
                    DiagBadAssignment(assign, lext ? " external @local!" : " internal @local!",
                        (rext ? " external" : " internal") + right->ty->String());
                }
            } else {
                DiagBadAssignment(assign, std::string{ToString(lLocal)}, right->ty->String());
            }
        }
    }

    bool IsCapture(const ASTContext& ctx, const Expr& expr)
    {
        auto ref = DynamicCast<RefExpr>(&expr);
        if (!ref) {
            return false;
        }
        auto target = ref->GetTarget();
        if (target->TestAnyAttr(Attribute::GLOBAL, Attribute::STATIC)) {
            return false;
        }
        auto defSite = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, target->scopeName);
        auto useSite = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, expr.scopeName);
        if (!defSite || !useSite) {
            return false;
        }
        return defSite->node != useSite->node;
    }

    void DiagBadAssignment(const Node& position, const std::string& arg1, const std::string& arg2)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_bad_local_assignment, position, arg1, arg2);
    }

    void CheckImplementsCopy(const InheritableDecl& decl)
    {
        for (auto& parent : decl.inheritedTypes) {
            if (!Ty::IsTyCorrect(parent->ty)) {
                continue;
            }
            if (parent->ty->kind == TypeKind::TYPE_COPY) {
                DiagImplementsCopy(*parent);
            }
        }
    }

    void DiagImplementsCopy(const Node& position)
    {
        d.Diagnose(position, DiagKind::sema_interface_is_not_implementable, std::string{COPY_NAME});
    }

    /// @~local var is always allowed
    /// @local? is allowed only if the class/struct has no constructor with this@~local type
    /// @local! is not allowed
    void CheckMemberVarModality(const InheritableDecl& decl)
    {
        // check whether this type has any constructor with @~local this type
        bool hasNotLocalThisCtor = HasNotLocalThisCtor(decl);
        for (auto member : decl.GetMemberDeclPtrs()) {
            if (auto var = DynamicCast<VarDecl>(member)) {
                CheckMemberVarModalType(*var, hasNotLocalThisCtor);
            }
        }
    }

    /// check whether a constructor has @~local this type (no explicit this param or explicit @~local)
    bool CtorHasNotLocalThis(const FuncDecl& ctor)
    {
        if (!Ty::IsTyCorrect(ctor.ty) || !ctor.funcBody) {
            return false; // Invalid constructor, skip
        }
        auto& paramList = ctor.funcBody->paramLists[0];
        if (!paramList->thisParam) {
            return true; // No this param means @~local this
        }
        auto local = paramList->thisParam->ty->modal.local;
        return local == LocalModal::NOT;
    }

    /// Check whether the type has any constructor with @~local this type
    /// If no constructors exist, there's an implicit default ctor with @~local this
    bool HasNotLocalThisCtor(const InheritableDecl& decl)
    {
        bool hasAnyCtor = false;
        for (auto member : decl.GetMemberDeclPtrs()) {
            if (auto func = DynamicCast<FuncDecl>(member)) {
                if (!Ty::IsTyCorrect(func->ty)) {
                    hasAnyCtor = true;
                    continue;
                }
                if (func->TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::PRIMARY_CONSTRUCTOR)) {
                    hasAnyCtor = true;
                    if (CtorHasNotLocalThis(*func)) {
                        return true;
                    }
                }
            }
        }
        // If no constructors, there's an implicit default ctor with @~local this
        return !hasAnyCtor;
    }

    void CheckMemberVarModalType(const VarDecl& var, bool hasNotLocalThisCtor)
    {
        if (!Ty::IsTyCorrect(var.ty) || var.TestAttr(Attribute::STATIC)) {
            return;
        }
        // @~local var is always allowed
        if (var.ty->modal.local == LocalModal::NOT) {
            return;
        }
        // @local! var is never allowed
        if (var.ty->modal.local == LocalModal::FULL) {
            DiagMemberVarLocalModalType(var);
            return;
        }
        // @local? var is only allowed if there's no constructor with @~local this
        if (var.ty->modal.local == LocalModal::HALF && hasNotLocalThisCtor) {
            DiagMemberVarLocalModalType(var);
        }
    }

    void DiagMemberVarLocalModalType(const VarDecl& var)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_member_var_local_type, var, var.identifier, var.ty->String(),
            var.ty->modal.local == LocalModal::HALF ? " when type has @~local constructor" : "");
    }

    /// Check global var cannot have local type
    void CheckGlobalVarModalType(const VarDecl& var)
    {
        if (var.TestAnyAttr(Attribute::GLOBAL, Attribute::STATIC)) {
            if (Ty::IsTyCorrect(var.ty) && var.ty->modal.local != LocalModal::NOT) {
                DiagGlobalVarLocalModalType(var);
            }
        }
    }

    void CheckCapture(const Node& func, const RefExpr& expr)
    {
        auto funcLocal = func.ty->modal.local;
        if (auto target = expr.GetTarget()) {
            if (type.ImplementsCopyInterface(target->ty)) {
                return;
            }
            auto varLocal = target->ty->modal.local;
            // these are the only allowed cases
            if (funcLocal == LocalModal::NOT && varLocal == LocalModal::NOT) {
                return;
            }
            if (funcLocal == LocalModal::HALF && varLocal != LocalModal::FULL) {
                return;
            }
            DiagBadCapture(func, expr);
        }
    }

    void CheckCaptures(const ASTContext& ctx, const Node& func)
    {
        auto body = GetFuncBody(func);
        Walker walker(body, [this, &ctx, &func](Node* node) {
            if (auto re = DynamicCast<RefExpr>(node)) {
                if (re->isThis || re->isSuper) {
                    if (auto localf = DynamicCast<FuncDecl>(&func); localf && Is<InheritableDecl>(localf->outerDecl)) {
                        return VisitAction::WALK_CHILDREN;
                    }
                    CheckCapture(func, *re);
                    return VisitAction::SKIP_CHILDREN;
                }
                if (auto target = DynamicCast<VarDecl>(re->GetTarget());
                    target && !target->TestAnyAttr(Attribute::STATIC, Attribute::GLOBAL)) {
                    auto targetDefSite =
                        ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, target->scopeName);
                    if (!targetDefSite || !targetDefSite->node) {
                        return VisitAction::WALK_CHILDREN;
                    }
                    if (targetDefSite->node == &func) {
                        return VisitAction::WALK_CHILDREN;
                    }
                    CheckCapture(func, *re);
                }
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
    }

    void DiagBadCapture(const Node& func, const NameReferenceExpr& capture)
    {
        auto db = d.DiagnoseRefactor(DiagKindRefactor::sema_capture_bad_local, capture, capture.ty->modal.LocalString(),
            capture.GetTarget()->identifier.Val(), func.ty->modal.LocalString(),
            func.astKind == ASTKind::FUNC_DECL ? "function" : "lambda");
    }

    static bool IsNonStaticMemberFunction(const FuncDecl& func)
    {
        if (func.ownerFunc ||
            func.TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::ENUM_CONSTRUCTOR, Attribute::PRIMARY_CONSTRUCTOR)) {
            return false;
        }
        // TODO: check prop
        return Is<InheritableDecl>(func.outerDecl) && !func.TestAttr(Attribute::STATIC);
    }

    const Expr* GetFuncArg(const CallExpr& call, size_t index)
    {
        if (auto inner = DynamicCast<CallExpr>(call.desugarExpr.get())) {
            return GetFuncArg(*inner, index);
        }
        if (auto array = DynamicCast<ArrayExpr>(call.desugarExpr.get())) {
            return array->args[index]->expr.get();
        }
        auto func = call.resolvedFunction;
        if (func && IsNonStaticMemberFunction(*func)) {
            // non static member function call, the first arg is this
            if (auto ma = DynamicCast<MemberAccess>(&*call.baseFunc)) {
                if (index == 0) {
                    return ma->baseExpr.get();
                }
                if (call.desugarArgs) {
                    return call.desugarArgs->at(index - 1)->expr.get();
                }
                return call.args[index - 1]->expr.get();
            }
            // RefExpr, using implicit this, no need to check
            return nullptr;
        }
        if (call.desugarArgs.has_value()) {
            return call.desugarArgs.value()[index]->expr.get();
        }
        return call.args[index]->expr.get();
    }

    struct FuncParamInfo {
        Ptr<Ty> ty;
        ModalInfo modal;
        std::string_view name;
    };
    static FuncParamInfo GetFuncParam(const CallExpr& call, size_t index)
    {
        if (auto inner = DynamicCast<CallExpr>(call.desugarExpr.get())) {
            return GetFuncParam(*inner, index);
        }
        if (auto array = DynamicCast<ArrayExpr>(call.desugarExpr.get())) {
            return {array->args[index]->ty, array->args[index]->ty->modal, ""};
        }
        auto func = call.resolvedFunction;
        if (!func) {
            // function pointer call, no need to check 'this' param, no arg name
            return {call.args[index]->ty, call.args[index]->ty->modal, ""};
        }
        if (call.baseFunc->ty->IsPointer()) {
            return {call.args[index]->ty, call.args[index]->ty->modal, ""};
        }
        if (func->funcBody->paramLists[0]->thisParam) {
            if (index == 0) {
                return {func->funcBody->paramLists[0]->thisParam->ty, func->funcBody->paramLists[0]->thisParam->modal,
                    "this"};
            }
            return {func->funcBody->paramLists[0]->params[index - 1]->ty,
                func->funcBody->paramLists[0]->params[index - 1]->ty->modal,
                func->funcBody->paramLists[0]->params[index - 1]->identifier.Val()};
        }
        if (IsNonStaticMemberFunction(*func)) {
            if (index == 0) {
                // TODO: get this type with modal
                return {func->outerDecl->ty, {}, "this"};
            }
            return {func->funcBody->paramLists[0]->params[index - 1]->ty,
                func->funcBody->paramLists[0]->params[index - 1]->ty->modal,
                func->funcBody->paramLists[0]->params[index - 1]->identifier.Val()};
        }
        return {func->funcBody->paramLists[0]->params[index]->ty,
            func->funcBody->paramLists[0]->params[index]->ty->modal,
            func->funcBody->paramLists[0]->params[index]->identifier.Val()};
    }

    size_t GetParamNum(const CallExpr& call)
    {
        if (auto inner = DynamicCast<CallExpr>(call.desugarExpr.get())) {
            return GetParamNum(*inner);
        }
        auto func = call.resolvedFunction;
        if (!func) {
            if (auto array = DynamicCast<ArrayExpr>(call.desugarExpr.get())) {
                return array->args.size();
            }
            if (call.baseFunc->ty->IsPointer()) {
                return call.args.size();
            }
            if (call.baseFunc->ty->kind == TypeKind::TYPE_CSTRING) {
                return 1UL;
            }
            // function pointer call, no need to check 'this' param
            if (auto fty = DynamicCast<FuncTy>(call.baseFunc->ty)) {
                return fty->paramTys.size();
            }
            // invalid
            return -1UL;
        }
        if (IsNonStaticMemberFunction(*func)) {
            return func->funcBody->paramLists[0]->params.size() + 1;
        }
        return func->funcBody->paramLists[0]->params.size();
    }

    std::unordered_map<FuncBody*, std::vector<const Expr*>> returnedExprMap;
    bool IsReturnedExpr(const ASTContext& ctx, const Expr& expr)
    {
        auto node = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, expr.scopeName);
        if (!node || !node->node) {
            return false;
        }
        auto body = GetFuncBody(*node->node);
        if (!body) {
            return false;
        }

        auto cache = returnedExprMap.find(body);
        auto& rets =
            cache != returnedExprMap.end() ? cache->second : (returnedExprMap[body] = CollectReturnedExpr(*body));
        return std::find(rets.begin(), rets.end(), &expr) != rets.end();
    }

    std::vector<const Expr*> CollectReturnedExpr(FuncBody& body)
    {
        if (!body.body || body.body->body.empty()) {
            return {};
        }
        std::vector<const Expr*> ret;
        Walker(&body, [&ret](auto n) {
            if (auto re = DynamicCast<ReturnExpr>(n)) {
                ret.push_back(re->expr.get());
                return VisitAction::SKIP_CHILDREN;
            }
            if (n->IsFuncLike()) {
                return VisitAction::SKIP_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        auto lastExpr = DynamicCast<Expr>(body.body->body.back().get());
        while (lastExpr && lastExpr->desugarExpr) {
            lastExpr = lastExpr->desugarExpr.get();
        }
        if (lastExpr->astKind == ASTKind::RETURN_EXPR) {
            return ret;
        }

        if (auto exclave = DynamicCast<ExclaveExpr>(lastExpr); exclave && !exclave->body->body.empty()) {
            lastExpr = DynamicCast<Expr>(exclave->body->body.back().get());
            while (lastExpr && lastExpr->desugarExpr) {
                // push last expr of exclave
                lastExpr = lastExpr->desugarExpr.get();
            }
        }
        // or push the last expr
        ret.push_back(lastExpr);
        return ret;
    }

    bool IsInExclaveExpr(const ASTContext& ctx, const Node& node)
    {
        auto sym = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(
            ctx, node.scopeName, [](Symbol& sym) { return sym.node->astKind == ASTKind::EXCLAVE_EXPR; });
        return sym && sym->node;
    }

    void DiagBadExternalLocalArg(const FuncParamInfo& param, const Expr& arg)
    {
        d.DiagnoseRefactor(
            DiagKindRefactor::sema_bad_external_local_arg, arg, arg.ty->modal.ToString(), std::string{param.name});
    }

    void DiagBadInternalLocalReturn(const Expr& expr)
    {
        d.DiagnoseRefactor(DiagKindRefactor::sema_bad_internal_local_return, expr);
    }

    /// Check call expr args cannot be external @local! type if the param is @local! nor Copy
    void CheckCallExpr(const ASTContext& ctx, const CallExpr& call)
    {
        size_t paramNum = GetParamNum(call);
        if (paramNum == -1UL) {
            // invalid call node, skip
            return;
        }
        for (size_t i = 0; i < paramNum; ++i) {
            auto arg = GetFuncArg(call, i);
            if (!arg) {
                continue;
            }
            auto param = GetFuncParam(call, i);
            if (param.modal.local == LocalModal::FULL) {
                if (IsExternalLocal(ctx, *arg) && !type.ImplementsCopyInterface(arg->ty)) {
                    DiagBadExternalLocalArg(param, *arg);
                }
            }
        }
    }

    void DiagGlobalVarLocalModalType(const VarDecl& var)
    {
        // use position of local modal if user wrote one
        auto pos = var.type ? MakeRange(var.type->modal.LocalBegin(), var.type->modal.LocalEnd())
                            : MakeRange(var.begin, var.end);
        d.DiagnoseRefactor(DiagKindRefactor::sema_global_var_local_modal, pos,
            var.TestAttr(Attribute::GLOBAL) ? "global" : "static", var.identifier.Val(),
            std::string{ToString(var.ty->modal.local)});
    }

    /// Check validity of @MakeCopy
    void CheckCopyType(StructDecl& decl)
    {
        for (auto member : decl.GetMemberDeclPtrs()) {
            if (auto var = DynamicCast<VarDecl>(member)) {
                if (type.ImplementsCopyInterface(var->ty)) {
                    continue;
                }
                DiagCopyStructBadField(decl, *var);
            }
        }
    }

    void DiagCopyStructBadField(const StructDecl& decl, const VarDecl& var)
    {
        d.DiagnoseRefactor(
            DiagKindRefactor::sema_copy_struct_bad_field, var, var.identifier.Val(), decl.identifier.Val());
    }

    DiagnosticEngine& d;
    TypeManager& type;
    std::vector<FuncContext> funcStack;
    Node* currentSpawnExpr = nullptr;       // Cache for tracking if we're inside a spawn block
    Node* currentGlobalStaticVar = nullptr; // Cache for tracking if we're inside a global/static var initializer
};

ModalTypeChecker* TypeChecker::TypeCheckerImpl::NewModalTypeChecker()
{
    return new ModalTypeChecker(diag, typeManager);
}

void TypeChecker::TypeCheckerImpl::DeleteModalTypeChecker()
{
    delete modalTypeChecker;
}

bool TypeChecker::TypeCheckerImpl::IsExternalLocal(const ASTContext& ctx, const Expr& expr)
{
    return modalTypeChecker->IsExternalLocal(ctx, expr);
}

void TypeChecker::TypeCheckerImpl::CheckModalType(const ASTContext& ctx, Package& pkg)
{
    modalTypeChecker->Check(ctx, pkg);
}

void TypeChecker::TypeCheckerImpl::ExpectSubtypeOf(
    Ptr<AST::Node> node, Ptr<AST::Ty> expect, Ptr<AST::Ty> actual, ModalMatchMode modal)
{
    if (!typeManager.IsSubtype(expect, actual, true, true, modal)) {
        Sema::DiagMismatchedTypesWithFoundTy(diag, *node, *expect, *actual);
        node->ty = TypeManager::GetInvalidTy();
    }
}

using namespace Sema;
bool TypeChecker::TypeCheckerImpl::ChkExclaveExpr(ASTContext& ctx, Ty& target, ExclaveExpr& expr)
{
    auto type = SynExclaveExpr(ctx, expr);
    if (!Ty::IsTyCorrect(type)) {
        return false;
    }
    // syn exclave either returns invalid, or returns Nothing, so subtype always holds
    // but in our design, InvalidTy is not a subtype of anything, so we still need to check
    if (!typeManager.IsSubtype(type, &target)) {
        DiagMismatchedTypes(diag, expr, target);
        expr.ty = TypeManager::GetInvalidTy();
        return false;
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::DiagSemaOutsideFunc(const ExclaveExpr& expr)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_exclave_outside_function, expr);
}

void TypeChecker::TypeCheckerImpl::DiagNestedExclave(const ExclaveExpr& expr, const AST::Node& outerNode)
{
    auto db = diag.DiagnoseRefactor(DiagKindRefactor::sema_nested_exclave, expr);
    db.AddHint(MakeRange(outerNode.begin, outerNode.end));
}

void TypeChecker::TypeCheckerImpl::DiagExpectedDataType(const AST::Node& node)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_expected_data_type, node, node.ty->String());
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynExclaveExpr(ASTContext& ctx, ExclaveExpr& expr)
{
    // Note: The check for "exclave is inside a function" (excluding default param values)
    // is now done in ModalTypeChecker::Check for better context tracking.

    // Find the enclosing function body to get the return type
    auto fun = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(ctx, expr.scopeName, [](Symbol& sym) {
        return sym.node->astKind == ASTKind::FUNC_BODY;
    });
    if (!fun || !fun->node) {
        // Error will be reported by ModalTypeChecker::Check
        // diag here, because the check in ModalTypeChecker::Check does not diag for node with InvalidTy
        DiagSemaOutsideFunc(expr);
        return expr.ty = TypeManager::GetInvalidTy();
    }

    // Check for nested exclave
    if (auto outerExclave = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(ctx, expr.scopeName,
            [e = &expr](Symbol& sym) { return sym.node != e && sym.node->astKind == ASTKind::EXCLAVE_EXPR; })) {
        DiagNestedExclave(expr, *outerExclave->node);
        // but we can still synthesize the type, so do not return invalid ty here
    }

    if (auto funcBodyTy = DynamicCast<FuncTy>(StaticCast<FuncBody>(fun->node)->ty)) {
        auto target= funcBodyTy->retTy;
        if (target->kind == TypeKind::TYPE_QUEST) {
            // func body does not have type, synthesize it and use it to synthesize function type later
            SynBlock({ctx, SynPos::EXPR_ARG}, *expr.body);
            return expr.ty = TypeManager::GetNothingTy();
        }
        if (auto func = StaticCast<FuncBody>(fun->node)->funcDecl) {
            if (func->TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::PRIMARY_CONSTRUCTOR) || func->IsFinalizer()) {
                // constructor and finalizer always returns Unit
                if (!ChkBlock(ctx, *TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT, {}), *expr.body)) {
                    return expr.ty = TypeManager::GetInvalidTy();
                }
                return expr.ty = TypeManager::GetNothingTy();
            }
        }
        if (Ty::IsTyCorrect(target)) {
            if (ChkBlock(ctx, *target, *expr.body)) {
                return expr.ty = TypeManager::GetNothingTy();
            }
            return expr.ty = TypeManager::GetInvalidTy();
        }
    }
    return expr.ty = TypeManager::GetInvalidTy();
}

void TypeChecker::TypeCheckerImpl::CheckHasLocalMod(const AST::Expr& node, AST::LocalModal local)
{
    if (!Ty::IsTyCorrect(node.ty)) {
        return;
    }
    if (node.ty->modal.local != local) {
        DiagLocalModNotSatisfied(node, local);
    }
}

void TypeChecker::TypeCheckerImpl::DiagLocalModNotSatisfied(const AST::Expr& node, AST::LocalModal local)
{
    diag.DiagnoseRefactor(
        DiagKindRefactor::sema_local_modal_not_satisfied, node, std::string{ToString(local)}, node.ty->String());
}
} // namespace Cangjie
