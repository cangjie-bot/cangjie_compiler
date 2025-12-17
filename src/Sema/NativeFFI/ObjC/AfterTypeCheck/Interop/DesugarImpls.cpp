// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of @ObjCImpl.
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Walker.h"
#include <iterator>

using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;
using namespace Cangjie::Interop::ObjC;

void DesugarImpls::HandleImpl(InteropContext& ctx)
{
    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        for (auto& memberDecl : impl->GetMemberDeclPtrs()) {
            if (memberDecl->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            switch (memberDecl->astKind) {
                case ASTKind::FUNC_DECL: {
                    auto& fd = *StaticAs<ASTKind::FUNC_DECL>(memberDecl);
                    Desugar(ctx, *impl, fd);
                    break;
                }
                case ASTKind::PROP_DECL: {
                    Desugar(ctx, *impl, *StaticAs<ASTKind::PROP_DECL>(memberDecl));
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void DesugarImpls::Desugar(InteropContext& ctx, ClassDecl& impl, FuncDecl& method)
{
    // We are interested in:
    // 1. CallExpr to MemberAccess, as it could be `super.<member>(...)`
    // 2. MemberAccess, as it could be a prop getter call
    // 3. CallExpr to RefExpr, as it could be `super(...)` or `this(...)`
    Walker(method.funcBody->body.get(), [&](auto node) {
        if (node->TestAnyAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }

        switch (node->astKind) {
            case ASTKind::CALL_EXPR:
                return DesugarCallExpr(ctx, impl, method, *StaticAs<ASTKind::CALL_EXPR>(node));
            case ASTKind::MEMBER_ACCESS:
                return DesugarGetForPropDecl(ctx, impl, method, *StaticAs<ASTKind::MEMBER_ACCESS>(node));
            default:
                break;
        }

        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void DesugarImpls::Desugar(InteropContext& ctx, ClassDecl& impl, PropDecl& prop)
{
    for (auto& getter : prop.getters) {
        Desugar(ctx, impl, *getter.get());
    }

    for (auto& setter : prop.setters) {
        Desugar(ctx, impl, *setter.get());
    }
}

VisitAction DesugarImpls::DesugarCallExpr(InteropContext& ctx, ClassDecl& impl, FuncDecl& method, CallExpr& ce)
{
    if (ce.TestAnyAttr(Attribute::UNREACHABLE, Attribute::LEFT_VALUE)) {
        return VisitAction::SKIP_CHILDREN;
    }

    if (ce.desugarExpr || !ce.baseFunc || !ce.resolvedFunction) {
        return VisitAction::WALK_CHILDREN;
    }

    if (ce.callKind != CallKind::CALL_SUPER_FUNCTION) {
        return VisitAction::WALK_CHILDREN;
    }

    auto targetFd = ce.resolvedFunction;
    if (targetFd->propDecl && targetFd->propDecl->TestAttr(Attribute::DESUGARED_MIRROR_FIELD)) {
        return VisitAction::WALK_CHILDREN;
    }

    auto targetFdTy = StaticCast<FuncTy>(targetFd->ty);
    auto curFile = ce.curFile;
    auto isInGeneratedCtor = ctx.factory.IsGeneratedCtor(method);
    /**
     * super(...args)
     * -->
     * if doesn't have @ObjCImpl super class:
     * super({
     *  self = [Impl alloc]; // skipped, if `self` is already provided
     *  self = [super init:...args];
     *  [self setRegistryId:putToRegistry(This)];
     *  self
     * }, ...args)
     *
     * else if has @ObjCImpl super class:
     * super({
     *   self = [Impl alloc]; // skipped, if `self` is already provided
     *   [super init:...args]
     * }, ...args)
     */
    if (IsSuperConstructorCall(ce)) {
        OwnedPtr<Expr> objCSelf;
        if (isInGeneratedCtor) {
            // already have a self ptr
            auto& methodParams = method.funcBody->paramLists[0]->params;
            CJC_ASSERT(methodParams.size() > 0);
            objCSelf = CreateRefExpr(*methodParams[0]);
        } else {
            // alloc a new ptr
            objCSelf = ctx.factory.CreateAllocCall(impl, curFile);
        }

        auto withMethodEnvCall = WithinFile(
            ctx.factory.CreateWithMethodEnvScope(std::move(objCSelf), targetFdTy->retTy,
                [&](auto&& receiver, auto&& objCSuper) {
                    std::vector<OwnedPtr<Expr>> superInitArgs;
                    std::transform(ce.args.begin(), ce.args.end(), std::back_inserter(superInitArgs), [&](auto& arg) {
                        return ctx.factory.UnwrapEntity(WithinFile(ASTCloner::Clone(arg->expr.get()), curFile));
                    });
                    auto superInit = ctx.factory.CreateMethodCallViaMsgSendSuper(
                        *targetFd, std::move(receiver), std::move(objCSuper), std::move(superInitArgs));

                    if (HasImplSuperClass(impl)) {
                        return Nodes<Node>(std::move(superInit));
                    }

                    auto tmpSelf = CreateTmpVarDecl(nullptr, std::move(superInit));
                    auto selfRef = CreateRefExpr(*tmpSelf);
                    auto putToRegistry =
                        ctx.factory.CreatePutToRegistryCall(CreateThisRef(Ptr(&impl), impl.ty, curFile));
                    auto setRegistryId =
                        ctx.factory.CreateObjCMsgSendCall(ASTCloner::Clone(selfRef.get()), REGISTRY_ID_SETTER_SELECTOR,
                            TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT), Nodes<Expr>(std::move(putToRegistry)));

                    return Nodes<Node>(std::move(tmpSelf), std::move(setRegistryId), std::move(selfRef));
                }),
            curFile);
        ce.desugarExpr = std::move(withMethodEnvCall);

        return VisitAction::WALK_CHILDREN;
    }

    /**
     * this(...args)
     * -->
     * if is in generated ctor:
     * this($obj, ...args)
     */
    if (isInGeneratedCtor && IsThisConstructorCall(ce)) {
        auto& methodParams = method.funcBody->paramLists[0]->params;
        CJC_ASSERT(methodParams.size() > 0);
        auto objCSelf = CreateRefExpr(*methodParams[0]);

        std::vector<OwnedPtr<FuncArg>> args;
        args.push_back(CreateFuncArg(std::move(objCSelf)));
        args.insert(args.end(), std::make_move_iterator(ce.args.begin()), std::make_move_iterator(ce.args.end()));

        auto realTarget = ctx.factory.GetGeneratedImplCtor(impl, *targetFd);
        auto realTargetTy = StaticCast<FuncTy>(realTarget->ty);
        ce.desugarExpr = CreateThisCall(impl, *realTarget, realTargetTy, curFile, std::move(args));

        return VisitAction::WALK_CHILDREN;
    }

    std::vector<OwnedPtr<Expr>> msgSendSuperArgs;
    std::transform(ce.args.begin(), ce.args.end(), std::back_inserter(msgSendSuperArgs),
        [&](auto& arg) { return ctx.factory.UnwrapEntity(WithinFile(ASTCloner::Clone(arg->expr.get()), curFile)); });

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(impl, false, ce.curFile);
    auto withMethodEnvCall = ctx.factory.CreateWithMethodEnvScope(
        std::move(nativeHandle), targetFdTy->retTy, [&](auto&& receiver, auto&& objCSuper) {
            OwnedPtr<Node> msgSendSuperCall;
            if (targetFd->propDecl) {
                if (!msgSendSuperArgs.empty()) {
                    msgSendSuperCall = ctx.factory.CreatePropSetterCallViaMsgSendSuper(
                        *targetFd->propDecl, std::move(receiver), std::move(objCSuper), std::move(msgSendSuperArgs[0]));
                } else {
                    msgSendSuperCall = ctx.factory.CreatePropGetterCallViaMsgSendSuper(
                        *targetFd->propDecl, std::move(receiver), std::move(objCSuper));
                }
            } else {
                msgSendSuperCall = ctx.factory.CreateMethodCallViaMsgSendSuper(
                    *targetFd, std::move(receiver), std::move(objCSuper), std::move(msgSendSuperArgs));
            }

            return Nodes<Node>(std::move(msgSendSuperCall));
        });
    withMethodEnvCall->curFile = curFile;
    ce.desugarExpr = ctx.factory.WrapEntity(std::move(withMethodEnvCall), *targetFdTy->retTy);
    return VisitAction::WALK_CHILDREN;
}

VisitAction DesugarImpls::DesugarGetForPropDecl(
    InteropContext& ctx, ClassDecl& impl, [[maybe_unused]] FuncDecl& method, MemberAccess& ma)
{
    if (ma.desugarExpr || ma.TestAnyAttr(Attribute::UNREACHABLE, Attribute::LEFT_VALUE)) {
        return VisitAction::SKIP_CHILDREN;
    }

    auto target = ma.GetTarget();
    if (!target || target->astKind != ASTKind::PROP_DECL || target->TestAttr(Attribute::DESUGARED_MIRROR_FIELD)) {
        return VisitAction::WALK_CHILDREN;
    }

    auto isSuper = false;
    if (auto re = As<ASTKind::REF_EXPR>(ma.baseExpr); re) {
        isSuper = re->isSuper;
    }

    if (!isSuper) {
        return VisitAction::WALK_CHILDREN;
    }

    auto pd = StaticAs<ASTKind::PROP_DECL>(target);
    if (!ctx.typeMapper.IsObjCMirror(*pd->outerDecl->ty)) {
        return VisitAction::WALK_CHILDREN;
    }
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(impl, false, ma.curFile);
    auto withMethodEnvCall =
        ctx.factory.CreateWithMethodEnvScope(std::move(nativeHandle), ma.ty, [&](auto&& receiver, auto&& objCSuper) {
            auto msgSendSuperCall =
                ctx.factory.CreatePropGetterCallViaMsgSendSuper(*pd, std::move(receiver), std::move(objCSuper));

            return Nodes<Node>(std::move(msgSendSuperCall));
        });
    withMethodEnvCall->curFile = ma.curFile;
    ma.desugarExpr = ctx.factory.WrapEntity(std::move(withMethodEnvCall), *ma.ty);
    return VisitAction::WALK_CHILDREN;
}
