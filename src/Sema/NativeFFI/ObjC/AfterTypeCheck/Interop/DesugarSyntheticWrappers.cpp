// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugar synthetic mirror interface wrappers.
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Interop::ObjC {
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

void DesugarSyntheticWrappers::HandleImpl(InteropContext& ctx)
{
    for (auto& wrapper : ctx.synWrappers) {
        if (wrapper->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        for (auto& memberDecl : wrapper->GetMemberDeclPtrs()) {
            if (memberDecl->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }

            if (ctx.factory.IsGeneratedMember(*memberDecl)) {
                continue;
            }

            memberDecl->DisableAttr(Attribute::ABSTRACT);
            switch (memberDecl->astKind) {
                case ASTKind::FUNC_DECL: {
                    auto& fd = *StaticAs<ASTKind::FUNC_DECL>(memberDecl);
                    if (fd.TestAttr(Attribute::FINALIZER)) {
                        continue;
                    }

                    DesugarMethod(ctx, *wrapper, fd);
                    break;
                }
                case ASTKind::PROP_DECL: {
                    auto& pd = *StaticAs<ASTKind::PROP_DECL>(memberDecl);
                    DesugarProp(ctx, *wrapper, pd);
                    break;
                }
                default:
                    break;
            }

            wrapper->DisableAttr(Attribute::ABSTRACT);
        }
    }
}

void DesugarSyntheticWrappers::DesugarMethod(InteropContext& ctx, ClassDecl& wrapper, FuncDecl& method)
{
    auto methodTy = StaticCast<FuncTy>(method.ty);
    auto curFile = method.curFile;

    if (method.TestAttr(Attribute::STATIC)) {
        // We are unable to provide a default implementation for the static method of an interface and therefore of its
        // synthetic wrapper.
        method.funcBody->body = CreateBlock(Nodes(ctx.factory.CreateThrowUnreachableCodeExpr(*curFile)),
            ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_NOTHING));
        return;
    }

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(wrapper, false, curFile);
    std::vector<OwnedPtr<Expr>> msgSendArgs;

    auto& params = method.funcBody->paramLists[0]->params;
    std::transform(params.begin(), params.end(), std::back_inserter(msgSendArgs),
        [&ctx, curFile](auto& param) { return ctx.factory.UnwrapEntity(WithinFile(CreateRefExpr(*param), curFile)); });

    auto arpScopeCall = ctx.factory.CreateAutoreleasePoolScope(methodTy->retTy,
        Nodes(ctx.factory.CreateMethodCallViaMsgSend(
            method, ASTCloner::Clone(nativeHandle.get()), std::move(msgSendArgs))));
    arpScopeCall->curFile = curFile;

    method.funcBody->body = CreateBlock({}, methodTy->retTy);

    if (method.HasAnno(AnnotationKind::OBJ_C_OPTIONAL)) {
        auto guardCall = ctx.factory.CreateOptionalMethodGuard(
            std::move(arpScopeCall), std::move(nativeHandle), method.identifier, curFile);
        guardCall->curFile = curFile;
        method.funcBody->body->body.emplace_back(std::move(guardCall));
    } else {
        method.funcBody->body->body.emplace_back(ctx.factory.WrapEntity(std::move(arpScopeCall), *methodTy->retTy));
    }
}

namespace {

void DesugarGetter(InteropContext& ctx, ClassDecl& wrapper, PropDecl& prop)
{
    CJC_ASSERT(!prop.getters.empty());
    auto& getter = prop.getters[0];
    auto curFile = prop.curFile;

    if (prop.TestAttr(Attribute::STATIC)) {
        // We are unable to provide a default implementation for the static property getter of an interface and
        // therefore of its synthetic wrapper.
        getter->funcBody->body = CreateBlock(Nodes(ctx.factory.CreateThrowUnreachableCodeExpr(*curFile)),
            ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_NOTHING));
        return;
    }

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(wrapper, false, curFile);
    auto arpScopeCall = ctx.factory.CreateAutoreleasePoolScope(
        prop.ty, Nodes(ctx.factory.CreatePropGetterCallViaMsgSend(prop, std::move(nativeHandle))));
    arpScopeCall->curFile = curFile;

    getter->funcBody->body = CreateBlock({}, prop.ty);
    getter->funcBody->body->body.emplace_back(ctx.factory.WrapEntity(std::move(arpScopeCall), *prop.ty));
}

void DesugarSetter(InteropContext& ctx, ClassDecl& wrapper, PropDecl& prop)
{
    CJC_ASSERT(prop.TestAttr(Attribute::MUT));
    CJC_ASSERT(!prop.setters.empty());
    auto& setter = prop.setters[0];
    auto curFile = prop.curFile;

    if (prop.TestAttr(Attribute::STATIC)) {
        // We are unable to provide a default implementation for the static property stter of an interface and therefore
        // of its synthetic wrapper.
        setter->funcBody->body = CreateBlock(Nodes(ctx.factory.CreateThrowUnreachableCodeExpr(*curFile)),
            ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_NOTHING));
        return;
    }

    auto unitTy = ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT);
    setter->funcBody->body = CreateBlock({}, unitTy);
    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(wrapper, false, curFile);
    auto paramRef = WithinFile(CreateRefExpr(*setter->funcBody->paramLists[0]->params[0]), curFile);
    auto arg = ctx.factory.UnwrapEntity(std::move(paramRef));

    auto arpScopeCall = ctx.factory.CreateAutoreleasePoolScope(
        unitTy, Nodes(ctx.factory.CreatePropSetterCallViaMsgSend(prop, std::move(nativeHandle), std::move(arg))));
    arpScopeCall->curFile = curFile;

    setter->funcBody->body->body.emplace_back(std::move(arpScopeCall));
}
} // namespace

void DesugarSyntheticWrappers::DesugarProp(InteropContext& ctx, ClassDecl& wrapper, PropDecl& prop)
{
    DesugarGetter(ctx, wrapper, prop);
    if (prop.TestAttr(Attribute::MUT)) {
        DesugarSetter(ctx, wrapper, prop);
    }
}

} // namespace Cangjie::Interop::ObjC
