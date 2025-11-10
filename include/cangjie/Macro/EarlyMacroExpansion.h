// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_EARLY_MACRO_EXPANSION_H
#define CANGJIE_EARLY_MACRO_EXPANSION_H

#include "cangjie/Macro/MacroExpansion.h"

namespace Cangjie {
class EarlyMacroExpansion : public MacroExpansion {
public:
    EarlyMacroExpansion(CompilerInstance* ci) : MacroExpansion{ci}
    {
    }

private:
    void ExecutePkg(AST::Package& package) override;
    void EvaluateMacros() override;
    void TransLateMacroInput(AST::Package& package);
}
} // namespace Cangjie

#endif