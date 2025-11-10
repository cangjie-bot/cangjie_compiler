// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Macro/LateMacroExpansion.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Macro/MacroEvaluation.h"
#include "cangjie/Sema/Desugar.h"
#include "cangjie/Sema/TypeChecker.h"

using namespace Cangjie;


void LateMacroExpansion::ExecutePkg(Package& package)
{
    std::cout << "### in late macroExpansion ###" << std::endl;
    curPackage = &package;
    // Collect macro-defs, macro-calls.
    CollectMacros(package);
    if (macroCollector.macCalls.empty() ||
        (ci->diag.GetErrorCount() > 0 && !ci->invocation.globalOptions.enableMacroInLSP)) {
        return;
    }

    ProcessCompileAddNode(package);

    EvaluateMacros();

    ProcessMacros(package);

    ReplaceAST(package);

    return;
}

void LateMacroExpansion::EvaluateMacros()
{
    bool useChildProcess = ci->invocation.globalOptions.enableMacroInLSP;
    MacroEvaluation evaluator(ci, &macroCollector, useChildProcess, true);
    evaluator.Evaluate();
}

void LateMacroExpansion::UnsetIsCheckVisitedAttr(Package& package)
{
    auto f = [](Ptr<Node> curNode) {
        curNode->DisableAttr(Attribute::IS_CHECK_VISITED);
        return VisitAction::WALK_CHILDREN;
    }
    Walker macroWalker(&package, f);
    macroWalker.Walk();
}
