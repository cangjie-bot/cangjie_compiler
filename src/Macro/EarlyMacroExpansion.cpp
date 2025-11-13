// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Macro/EarlyMacroExpansion.h"
#include "cangjie/Macro/MacroEvaluation.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace AST;

namespace {
bool HasNoMacros(const MacroCollector& mc)
{
    return mc.macroDefFuncs.empty() && mc.macCalls.empty();
}

bool HasNoMacroCalls(const std::vector<MacroCall>& macCalls)
{
    return macCalls.empty();
}

bool HasDefAndCallInSamePkg(const MacroCollector& macroCollector, DiagnosticEngine& diag)
{
    // macro-def and macro-call can't be in the same package.
    bool ret = false;
    std::unordered_set<std::string> s1;
    for (auto fd : macroCollector.macroDefFuncs) {
        (void)s1.insert(fd->identifier);
    }
    for (auto call : macroCollector.macCalls) {
        if (s1.find(call.GetFullName()) != s1.end()) {
            ret = true;
            (void)diag.Diagnose(call.GetBeginPos(), DiagKind::macro_unexpect_def_and_call_in_same_pkg);
        }
    }
    return ret;
}

std::string GetPrimaryName(const MacroInvocation& invocation)
{
    if (invocation.decl) {
        if (invocation.decl->astKind == ASTKind::STRUCT_DECL || invocation.decl->astKind == ASTKind::CLASS_DECL) {
            return invocation.decl->identifier.Val();
        } else {
            return invocation.outerDeclIdent;
        }
    }
    return "";
}
}

std::mutex globalMacroExpandLock;

void EarlyMacroExpansion::ExecutePkg(Package& package)
{
    std::cout << "### in early macroExpansion ###" << std::endl;
    std::lock_guard<std::mutex> guard(globalMacroExpandLock);
    curPackage = &package;
    // Collect macro-defs, macro-calls.
    CollectMacros(package);
    if (HasNoMacros(this->macroCollector) || HasNoMacroCalls(this->macroCollector.macCalls) ||
        HasDefAndCallInSamePkg(this->macroCollector, ci->diag) ||
        (ci->diag.GetErrorCount() > 0 && !ci->invocation.globalOptions.enableMacroInLSP)) {
        return;
    }
    // Evaluate macros. Generate new tokens for further AST replacement.
    EvaluateMacros();
    // Map macro information and save the expanded macro contents to a file.
    ProcessMacros(package);
    // Replace MacroCall AST with new generated AST.
    ReplaceAST(package);
    // Translate macro input nodes.
    TransLateMacroInput(package);
}

void EarlyMacroExpansion::EvaluateMacros()
{
    bool useChildProcess = ci->invocation.globalOptions.enableMacroInLSP;
    MacroEvaluation evaluator(ci, &macroCollector, useChildProcess, false);
    evaluator.Evaluate();
    tokensEvalInMacro = evaluator.GetVecOfGeneratedCodes();
}

void EarlyMacroExpansion::TransLateMacroInput(Package& package)
{
    CollectMacros(package);
    for (auto &macroCall : macroCollector.macCalls) {
        auto pInvocation = macroCall.GetInvocation();
        if (!macroCall.GetNode()->TestAttr(Attribute::LATE_MACRO)) {
            continuel
        }
        if (pInvocation->hasParenthesis) {
            std::vector<Token> newTokens = pInvocation->args;
            Parser parser(newTokens, ci->diag, ci->diag.GetSourceManager());
            (void)parser.SetPrimaryDecl(GetPrimaryName(*pInvocation)).SetCurFile(macroCall.GetNode()->curFile);
            auto expr = parser.ParseExpr();
            pInvocation->expr = std::move(expr);
        } else if (pInvocation->decl == nullptr) {
            std::vector<Token> newTokens = pInvocation->args;
            Parser parser(newTokens, ci->diag, ci->diag.GetSourceManager());
            (void)parser.SetPrimaryDecl(GetPrimaryName(*pInvocation)).SetCurFile(macroCall.GetNode()->curFile);
            auto scopeKind = std::get_if<ScopeKind>(&(pInvocation->scope));
            auto decl = parser.ParseDecl(*scopeKind);
            pInvocation->decl = std::move(decl);
        }
    }
}