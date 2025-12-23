// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements TranslatorContext.
 */

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/TranslatorContext.h"
#include "cangjie/AST/ASTCasting.h"

namespace Cangjie::CHIR {
using namespace AST;
TranslatorContext::TranslatorContext() = default;

TranslatorContext::~TranslatorContext() = default;

TranslatorContext::TranslatorContext(const TranslatorContext& other) = default;

void TranslatorContext::PushFunc(const AST::FuncDecl& func, Func& fun)
{
    funcStack.emplace_back(FuncContext{TranslatorContextKind::FUNC, &func, &fun});
}

void TranslatorContext::PushFunc(const AST::LambdaExpr& func, Lambda& lambda)
{
    funcStack.emplace_back(FuncContext{TranslatorContextKind::FUNC, &func, &lambda});
}

void TranslatorContext::PushFunc(const AST::FuncDecl& func, Lambda& lambda)
{
    funcStack.emplace_back(FuncContext{TranslatorContextKind::FUNC, &func, &lambda});
}

void TranslatorContext::PushGlobalVar(const AST::VarDeclAbstract& var, Func& fun)
{
    funcStack.emplace_back(FuncContext{TranslatorContextKind::GLOBAL_VAR, &var, &fun});
}

void TranslatorContext::Pop()
{
    CJC_ASSERT(!funcStack.empty());
    funcStack.pop_back();
}

bool TranslatorContext::NeedsRegion() const
{
    if (funcStack.empty()) {
        return false;
    }
    auto& front = funcStack.front();
    if (front.kind == TranslatorContextKind::FUNC) {
        if (auto lambda = DynamicCast<LambdaExpr>(front.node)) {
            return lambda->needsRegion;
        }
        if (auto func = DynamicCast<FuncDecl>(front.node)) {
            return func->needsRegion;
        }
    }
    return false;
}
} // namespace Cangjie::CHIR
