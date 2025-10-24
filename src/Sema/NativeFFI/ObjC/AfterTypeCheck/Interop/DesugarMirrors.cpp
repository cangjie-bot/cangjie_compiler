// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of Objective-C mirror declarations.
 */

#include "Handlers.h"
#include "NativeFFI/Utils.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/CheckUtils.h"
#include <iterator>

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

void DesugarMirrors::HandleImpl(InteropContext& ctx)
{
    for (auto& mirror : ctx.mirrors) {
        if (mirror->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        for (auto& memberDecl : mirror->GetMemberDeclPtrs()) {
            if (memberDecl->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            if (ctx.factory.IsGeneratedMember(*memberDecl)) {
                continue;
            }

            switch (memberDecl->astKind) {
                case ASTKind::FUNC_DECL: {
                    auto& fd = *StaticAs<ASTKind::FUNC_DECL>(memberDecl);
                    if (fd.TestAttr(Attribute::CONSTRUCTOR)) {
                        DesugarCtor(ctx, *mirror, fd);
                    } else if (IsStaticInitMethod(fd)) {
                        DesugarStaticInitializer(ctx, fd);
                    } else {
                        DesugarMethod(ctx, *mirror, fd);
                    }
                    break;
                }
                case ASTKind::PROP_DECL: {
                    auto& pd = *StaticAs<ASTKind::PROP_DECL>(memberDecl);
                    if (memberDecl->TestAttr(Attribute::DESUGARED_MIRROR_FIELD)) {
                        DesugarField(ctx, *mirror, pd);
                    } else {
                        DesugarProp(ctx, *mirror, pd);
                    }
                    break;
                }
                case ASTKind::VAR_DECL:
                    // Unreachable, because all @ObjCMirror fields are converted to props on previous stages.
                    CJC_ABORT();
                    break;
                default:
                    break;
            }
        }
    }
}

void DesugarMirrors::DesugarCtor(InteropContext& ctx, ClassLikeDecl& mirror, FuncDecl& ctor)
{ 
    CJC_ASSERT(ctor.TestAttr(Attribute::CONSTRUCTOR));
    auto curFile = ctor.curFile;
    auto& generatedCtor = *ctx.factory.GetGeneratedMirrorCtor(mirror);
    auto thisCall = CreateThisCall(mirror, generatedCtor, generatedCtor.ty, curFile);

    auto initCall = ctx.factory.CreateAllocInitCall(ctor);
    thisCall->args.emplace_back(CreateFuncArg(std::move(initCall)));

    ctor.constructorCall = ConstructorCall::OTHER_INIT;
    ctor.funcBody->body->body.emplace_back(std::move(thisCall));
}

void DesugarMirrors::DesugarStaticInitializer(InteropContext& ctx, FuncDecl& initializer)
{
    CJC_ASSERT(IsStaticInitMethod(initializer));
    auto curFile = initializer.curFile;

    auto initCall = ctx.factory.CreateAllocInitCall(initializer);
    auto returnExpr = WithinFile(CreateReturnExpr(std::move(initCall)), curFile);
    returnExpr->ty = TypeManager::GetNothingTy();
    initializer.funcBody->body->body.emplace_back(std::move(returnExpr));
}

void DesugarMirrors::DesugarMethod(InteropContext& ctx, ClassLikeDecl& mirror, FuncDecl& method)
{
    auto methodTy = StaticCast<FuncTy>(method.ty);
    auto curFile = method.curFile;
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, method.TestAttr(Attribute::STATIC), curFile);
    std::vector<OwnedPtr<Expr>> msgSendArgs;

    auto& params = method.funcBody->paramLists[0]->params;
    std::transform(params.begin(), params.end(), std::back_inserter(msgSendArgs),
        [&ctx, curFile](auto& param) { return ctx.factory.UnwrapEntity(WithinFile(CreateRefExpr(*param), curFile)); });

    auto arpScopeCall = ctx.factory.CreateAutoreleasePoolScope(methodTy->retTy,
        Nodes(ctx.factory.CreateMethodCallViaMsgSend(method, std::move(nativeHandle), std::move(msgSendArgs))));

    method.funcBody->body = CreateBlock({}, methodTy->retTy);
    method.funcBody->body->body.emplace_back(ctx.factory.WrapEntity(std::move(arpScopeCall), *methodTy->retTy));
}

namespace {

void DesugarGetter(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& prop)
{
    CJC_ASSERT(!prop.getters.empty());
    auto& getter = prop.getters[0];
    auto curFile = prop.curFile;

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, prop.TestAttr(Attribute::STATIC), curFile);

    auto arpScopeCall = ctx.factory.CreateAutoreleasePoolScope(
        prop.ty, Nodes(ctx.factory.CreatePropGetterCallViaMsgSend(prop, std::move(nativeHandle))));

    getter->funcBody->body = CreateBlock({}, prop.ty);
    getter->funcBody->body->body.emplace_back(ctx.factory.WrapEntity(std::move(arpScopeCall), *prop.ty));
}

void DesugarSetter(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& prop)
{
    CJC_ASSERT(prop.TestAttr(Attribute::MUT));
    CJC_ASSERT(!prop.setters.empty());
    auto& setter = prop.setters[0];
    auto curFile = prop.curFile;
    auto unitTy = ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT);
    setter->funcBody->body = CreateBlock({}, unitTy);

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, prop.TestAttr(Attribute::STATIC), curFile);
    auto paramRef = WithinFile(CreateRefExpr(*setter->funcBody->paramLists[0]->params[0]), curFile);
    auto arg = ctx.factory.UnwrapEntity(std::move(paramRef));

    auto arpScopeCall = ctx.factory.CreateAutoreleasePoolScope(
        unitTy, Nodes(ctx.factory.CreatePropSetterCallViaMsgSend(prop, std::move(nativeHandle), std::move(arg))));

    setter->funcBody->body->body.emplace_back(std::move(arpScopeCall));
}

void DesugarFieldGetter(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& field)
{
    CJC_ASSERT(!field.getters.empty());
    auto& getter = field.getters[0];
    auto curFile = field.curFile;
    getter->funcBody->body = CreateBlock({}, field.ty);

    CJC_ASSERT(!field.TestAttr(Attribute::STATIC));
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, false, curFile);

    auto getInstanceVariableCall = ctx.factory.CreateGetInstanceVariableCall(field, std::move(nativeHandle));

    getter->funcBody->body->body.emplace_back(ctx.factory.WrapEntity(std::move(getInstanceVariableCall), *field.ty));
}

void DesugarFieldSetter(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& field)
{
    CJC_ASSERT(field.TestAttr(Attribute::MUT));
    CJC_ASSERT(!field.setters.empty());
    auto& setter = field.setters[0];
    auto curFile = field.curFile;
    auto unitTy = ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT);
    setter->funcBody->body = CreateBlock({}, unitTy);

    CJC_ASSERT(!field.TestAttr(Attribute::STATIC));
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(mirror, false, curFile);
    auto paramRef = WithinFile(CreateRefExpr(*setter->funcBody->paramLists[0]->params[0]), curFile);
    auto arg = ctx.factory.UnwrapEntity(std::move(paramRef));

    auto setInstanceVariableCall =
        ctx.factory.CreateObjCRuntimeSetInstanceVariableCall(field, std::move(nativeHandle), std::move(arg));

    setter->funcBody->body->body.emplace_back(std::move(setInstanceVariableCall));
}
} // namespace

void DesugarMirrors::DesugarProp(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& prop)
{
    DesugarGetter(ctx, mirror, prop);
    if (prop.TestAttr(Attribute::MUT)) {
        DesugarSetter(ctx, mirror, prop);
    }
}

void DesugarMirrors::DesugarField(InteropContext& ctx, ClassLikeDecl& mirror, PropDecl& field)
{
    DesugarFieldGetter(ctx, mirror, field);
    if (field.TestAttr(Attribute::MUT)) {
        DesugarFieldSetter(ctx, mirror, field);
    }
}
