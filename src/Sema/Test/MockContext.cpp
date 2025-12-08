// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "MockContext.h"

namespace Cangjie {

MockContext::MockContext() : mangler(), manglerCtxs()
{
}

MockContext::~MockContext()
{
    for (auto& [pkgName, manglerCtx] : manglerCtxs) {
        mangler.manglerCtxTable.erase(pkgName);
    }
}

void MockContext::PrepareManglerContext(Ptr<AST::Package> pkg)
{
    manglerCtxs.emplace(
        ManglerContext::ReduceUnitTestPackageName(pkg->fullPackageName), mangler.PrepareContextForPackage(pkg));
}

BaseMangler& MockContext::GetMangler()
{
    return mangler;
}

} // namespace Cangjie
