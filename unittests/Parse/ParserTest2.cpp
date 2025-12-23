// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/AST/Node.h"
#include "cangjie/Parse/Parser.h"
#include "cangjie/Utils/CastingTemplate.h"
#include <gtest/gtest.h>
#include <string>

#define PARSER_TEST_COMMON(c)                                                                                          \
    std::string code = c;                                                                                              \
    SourceManager sm;                                                                                                  \
    DiagnosticEngine diag;                                                                                             \
    diag.SetSourceManager(&sm);                                                                                        \
    Parser parser(code, diag, sm);                                                                                     \
    auto file = parser.ParseTopLevel()

using namespace Cangjie;
using namespace AST;

TEST(ParserTest1, LocalNot)
{
    PARSER_TEST_COMMON("let a: int @~local = 10");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    auto& a = StaticCast<VarDecl>(*file->decls[0]);
    EXPECT_EQ(a.type->modal.Local(), LocalModal::NOT);
    EXPECT_EQ(a.type->modal.LocalBegin(), Position(1, 12));
}

TEST(ParserTest1, LocalHalf)
{
    PARSER_TEST_COMMON("let a: int @local? = 10");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    auto& a = StaticCast<VarDecl>(*file->decls[0]);
    EXPECT_EQ(a.type->modal.Local(), LocalModal::HALF);
    EXPECT_EQ(a.type->modal.LocalBegin(), Position(1, 12));
}

TEST(ParserTest1, LocalFull)
{
    PARSER_TEST_COMMON("let a: int @local! = 10");
    ASSERT_EQ(diag.GetErrorCount(), 0);
    auto& a = StaticCast<VarDecl>(*file->decls[0]);
    EXPECT_EQ(a.type->modal.Local(), LocalModal::FULL);
    EXPECT_EQ(a.type->modal.LocalBegin(), Position(1, 12));
}

TEST(ParserTest1, LocalNotAfterAt)
{
    PARSER_TEST_COMMON("let a: int @~ local = 10");
    auto diags = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    ASSERT_EQ(diags.size(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_one_of_identifier_or_pattern);
}

TEST(ParserTest1, LocalHalfAfterAt)
{
    PARSER_TEST_COMMON("let a: int @local ? = 10");
    auto diags = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    ASSERT_EQ(diags.size(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_one_of_identifier_or_pattern);
}
