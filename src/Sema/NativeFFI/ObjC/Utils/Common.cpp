// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements common utils for Cangjie <-> Objective-C interop.
 */

#include "Common.h"
#include "ASTFactory.h"
#include "TypeMapper.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

namespace {
using namespace Cangjie;

Ptr<ClassDecl> GetMirrorSuperClass(const ClassLikeDecl& target)
{
    if (auto classDecl = DynamicCast<const ClassDecl*>(&target)) {
        auto superClass = classDecl->GetSuperClassDecl();
        if (superClass && TypeMapper::IsValidObjCMirror(*superClass->ty)) {
            return superClass;
        }
    }

    return nullptr;
}

} // namespace

bool Cangjie::Interop::ObjC::HasMirrorSuperClass(const ClassLikeDecl& target)
{
    return GetMirrorSuperClass(target) != nullptr;
}
