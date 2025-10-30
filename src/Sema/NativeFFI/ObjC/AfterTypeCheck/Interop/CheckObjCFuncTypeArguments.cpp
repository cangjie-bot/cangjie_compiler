// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements checks of types used with ObjCPointer
 */


#include "NativeFFI/Utils.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "cangjie/AST/Walker.h"
#include "Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void CheckObjCFuncTypeArguments::HandleImpl(InteropContext& ctx)
{
    for (auto& file : ctx.pkg.files) {
        Walker(file, Walker::GetNextWalkerID(), [&file, &ctx](auto node) {
            if (!node->IsSamePackage(*file->curPackage)) {
                return VisitAction::WALK_CHILDREN;
            }
            if (Ptr<Decl> decl = As<ASTKind::DECL>(node);
                decl && ctx.typeMapper.IsObjCFuncOrBlock(*decl)) {
                return VisitAction::SKIP_CHILDREN;
            }
            Ptr<Type> typeUsage = As<ASTKind::TYPE>(node);
            if (typeUsage
             && typeUsage->ty
             && typeUsage->ty->typeArgs.size() == 1
             && ctx.typeMapper.IsObjCFuncOrBlock(*typeUsage->ty)) {
                auto tyArg = typeUsage->ty->typeArgs[0];
                auto valid = tyArg->IsFunc();
                valid &= !tyArg->IsCFunc();
                for (auto subTy : tyArg->typeArgs) {
                    valid &= ctx.typeMapper.IsObjCCompatible(*subTy);
                }
                if (!valid) {
                    Ptr<Node> errorRef =
                        typeUsage->GetTypeArgs().size() > 0?
                            typeUsage->GetTypeArgs()[0] : typeUsage;
                    ctx.diag.DiagnoseRefactor(
                        DiagKindRefactor::sema_objc_func_argument_must_be_objc_compatible,
                        *errorRef,
                        Ty::GetDeclOfTy(typeUsage->ty)->identifier.Val());
                    typeUsage->EnableAttr(Attribute::IS_BROKEN);
                }
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}
