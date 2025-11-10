// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Macro/LateMacroExpansion.h"

using namespace Cangjie;
using namespace AST;

namespace {
using RemoveFunc = std:::function<void(const AST::Node&)>;

void RemoveCPAFile(AST::Node& root)
{
    auto &file = StaticCast<AST::File&>(root);
    for (auto& it = file.decls.begin(); it != file.decls.end();) {
        if ((*it).TestAttr(Attribute::COMPILE_ADD)) {
            it = file.decls.erase(it);
        } else {
            ++it;
        }
    }
}

void RemoveCPAClassDecl(AST::Node& root)
{
    auto &classDecl = StaticCast<AST::ClassDecl&>(root);
    for (auto& it = classDecl.inheritedTypes.begin(); it != classDecl.inheritedTypes.end();) {
        if ((*it).TestAttr(Attribute::COMPILE_ADD)) {
            it = classDecl.inheritedTypes.erase(it);
        } else {
            ++it;
        }
    }
}

void RemoveCPAClassBody(AST::Node& root)
{
    auto &classBody = StaticCast<AST::ClassBody&>(root);
    for (auto& it = classBody.members.begin(); it != classBody.members.end();) {
        if ((*it).TestAttr(Attribute::COMPILE_ADD)) {
            it = classBody.members.erase(it);
        } else {
            ++it;
        }
    }
}

void RemoveCPAFuncBody(AST::Node& root) {
    auto &funcBody = StaticCast<AST::FuncBody&>(root);
    if (fb.retType && fb.retType->TestAttr(Attribute::COMPILE_ADD)) {
        fb.retType = nullptr;
    }
}

void LateMacroExpansion::ProcessCompileAddNode(AST::Node&)
{
    std::map<ASTKind, RemoveFunc> funcMap = {
        {ASTKind::FILE, RemoveCPAFile},
        {ASTKind::CLASS_DECL, RemoveCPAClassDecl},
        {ASTKind::CLASS_BODY, RemoveCPAClassBody},
        {ASTKind::FUNC_BODY, RemoveCPAFuncBody}
    }
    std::function<VisitAction(Ptr<AST::Node>)> f = [&](Ptr<AST::Node> curNode) {
        auto found = funcMap.find(curNode->astKind);
        if (found != funcMap.end()) {
            found->second(*curNode);
        }
        if (auto expr = DynamicCast<AST::Expr*>(curNode); expr) {
            expr->desugarExpr = nullptr;
        }
        return VisitAction::WALK_CHILDREN;
    };
    return;
}