// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/SourceManager.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;

// Macro to set up parser infrastructure and parse code
// Usage: PARSE_CODE(code) - sets up diag, sm, parser, and parses the code
#define PARSE_CODE(codeStr) \
    std::string code = codeStr; \
    SourceManager sm; \
    DiagnosticEngine diag; \
    diag.SetSourceManager(&sm); \
    Parser parser{code, diag, sm}; \
    auto file = parser.ParseTopLevel(); \
    auto diags = diag.GetCategoryDiagnostic(DiagCategory::PARSE)

TEST(ParserTest1, PrematureEndAnnotation)
{
    PARSE_CODE("@Anno[");
    ASSERT_EQ(diag.GetErrorCount(), 2);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_decl);
    EXPECT_EQ(diags[1].rKind, DiagKindRefactor::parse_expected_right_delimiter);
}

TEST(ParserTest1, PrematureEndTupleLiteral)
{
    PARSE_CODE("((");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_decl);
}

TEST(ParserTest1, PrematureEndFor)
{
    PARSE_CODE("for (a in 1..10");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_decl);
}

TEST(ParserTest1, PrematureEndDoWhile)
{
    PARSE_CODE("do { i++ } while (true");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_decl);
}

TEST(ParserTest1, PrematureEndSpawn)
{
    PARSE_CODE("spawn(mainThreadContext");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_decl);
}

TEST(ParserTest1, SpawnLambda)
{
    PARSE_CODE("spawn @OverflowWrapping { a + b }");
    EXPECT_EQ(diag.GetErrorCount(), 3);
}

TEST(ParserTest1, PrematureEndSynchronized)
{
    PARSE_CODE("synchronized(a");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_decl);
}

TEST(ParserTest1, PrematureEndTypeConversion)
{
    PARSE_CODE("let a = Int64 3");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_expression);
}

TEST(ParserTest1, PrematureEndTypeConversion2)
{
    PARSE_CODE("let a = Int64(3");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_right_delimiter);
}

TEST(ParserTest1, FinalizerWithParams)
{
    PARSE_CODE("class C { ~init(a: Int64) {} }");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_finalizer_can_not_accept_any_parameter);
}

TEST(ParserTest1, PrematureEndTypeAlias)
{
    PARSE_CODE("type A ");
    ASSERT_EQ(diag.GetErrorCount(), 1);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::parse_expected_assignment);
}