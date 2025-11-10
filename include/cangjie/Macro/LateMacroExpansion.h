// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_LATE_MACRO_EXPANSION_H
#define CANGJIE_LATE_MACRO_EXPANSION_H

#include "cangjie/Macro/MacroExpansion.h"

namespace Cangjie {
class LateMacroExpansion : public MacroExpansion {
public:
    LateMacroExpansion(CompilerInstance* ci) : MacroExpansion(ci)
    {
    }

private:
    void ExecutePkg(AST::Package& package) override;
    void EvaluateMacros() override;
    void ProcessCompileAddNode(AST::Node& root);
    void UnsetIsCheckVisitedAttr(AST::Package& package);
};
#endif