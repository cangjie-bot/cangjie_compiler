// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares TranslatorContext.
 */

#ifndef CANGJIE_CHIR_TRANSLATOR_CONTEXT_H
#define CANGJIE_CHIR_TRANSLATOR_CONTEXT_H

#include "cangjie/AST/Node.h"
#include "cangjie/CHIR/IR/Expression/Expression.h"

namespace Cangjie::CHIR {
enum class TranslatorContextKind {
    FUNC,       // any func like node
    GLOBAL_VAR, // global var or static member var
};

struct FuncContext {
    TranslatorContextKind kind;
    const AST::Node* node;
    Base* func; // Func or Lambda
};

class TranslatorContext {
public:
    TranslatorContext();
    TranslatorContext(const TranslatorContext&);
    ~TranslatorContext();

    void PushFunc(const AST::FuncDecl& func, Func& fun);
    void PushFunc(const AST::LambdaExpr& func, Lambda& lambda);
    void PushFunc(const AST::FuncDecl& func, Lambda& lambda);
    void PushGlobalVar(const AST::VarDeclAbstract& var, Func& fun);
    void Pop();
    bool NeedsRegion() const;

private:
    /// this stack is usually very small, so we can copy it by value
    std::list<FuncContext> funcStack;
};

} // namespace Cangjie::CHIR

#endif // CANGJIE_CHIR_TRANSLATOR_CONTEXT_H