// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Lexer.
 */

#include "LexerImpl.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "cangjie/Basic/Display.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Utils/Unicode.h"

using namespace Cangjie;
namespace {
inline bool IsLegalEscape(int32_t ch)
{
    const std::string legalEscape("tbrnfv0'\"\\");
    auto pos = legalEscape.find(static_cast<char>(ch));
    return pos != std::string::npos;
}

inline bool IsMacroEscape(char ch)
{
    const std::string macroEscape("$@()[]");
    auto pos = macroEscape.find(ch);
    return pos != std::string::npos;
}

inline bool IsSingleQuote(int32_t ch)
{
    return ch == '\'';
}
} // namespace

namespace Cangjie {
const std::vector<TokenKind>& GetContextualKeyword()
{
    static const std::vector<TokenKind> CONTEXTUAL_KEYWORD_TOKEN = {TokenKind::PUBLIC, TokenKind::PRIVATE,
        TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::OVERRIDE, TokenKind::REDEF, TokenKind::ABSTRACT,
        TokenKind::SEALED, TokenKind::OPEN, TokenKind::COMMON, TokenKind::PLATFORM, TokenKind::FEATURES};
    return CONTEXTUAL_KEYWORD_TOKEN;
}
bool IsContextualKeyword(std::string_view s)
{
    static std::unordered_set<std::string_view> names{};
    if (names.empty()) {
        for (auto ct : GetContextualKeyword()) {
            names.insert(TOKENS[static_cast<int>(ct)]);
        }
    }
    return names.count(s) == 1;
}
}

bool LexerImpl::IsCurrentCharLineTerminator() const
{
    if (Utils::GetLineTerminatorLength(pCurrent, pInputEnd) > 0) {
        return true;
    }
    return false;
}

Lexer::Lexer(unsigned int fileID, const std::string& input, DiagnosticEngine& diag, SourceManager& sm, bool cts)
    : impl{new LexerImpl{input, diag, sm, {.fileID = fileID, .posBase{fileID, 1, 1}, .collectTokenStream = cts}}}
{
}

Lexer::Lexer(
    unsigned int fileID, const std::string& input, DiagnosticEngine& diag, SourceManager& sm, const Position& pos)
    : impl{new LexerImpl{input, diag, sm, {.fileID = fileID, .posBase{pos}}}}
{
}

Lexer::Lexer(const std::string& input, DiagnosticEngine& diag, SourceManager& sm, bool cts, bool splitAmbi)
    : impl{new LexerImpl{input, diag, sm, {.collectTokenStream = cts, .splitAmbiguousToken = splitAmbi}}}
{
}

Lexer::Lexer(const std::string& input, DiagnosticEngine& diag, SourceManager& sm, const Position& pos, bool cts)
    : impl{new LexerImpl{input, diag, sm, {.posBase{pos}, .collectTokenStream = cts}}}
{
}

Lexer::Lexer(const std::string& input, DiagnosticEngine& diag, Source& s, const Position& pos, bool cts)
    : impl{new LexerImpl{input, diag, s, {.posBase{pos}, .collectTokenStream = cts}}}
{
}

Lexer::Lexer(const std::vector<Token>& inputTokens, DiagnosticEngine& diag, SourceManager& sm, bool cts)
    : impl{new LexerImpl{inputTokens, diag, sm, {.collectTokenStream = cts}}}
{
}

Lexer::~Lexer()
{
    delete impl;
}

const Position& Token::End() const
{
    return end;
}

bool LexerImpl::CheckArraySize(size_t len, int32_t& ch)
{
    if (pNext + len > pInputEnd) {
        if (success) {
            int32_t cha = static_cast<int32_t>(static_cast<uint32_t>((reinterpret_cast<const uint8_t*>(pNext))[0]));
            diag.DiagnoseRefactor(DiagKindRefactor::lex_illegal_UTF8_encoding_byte, GetPos(pNext),
                ToBinaryString(static_cast<uint8_t>(cha)));
            success = false;
        }
        pNext = pInputEnd;
        ch = ERROR_UTF8;
        return false;
    }
    return true;
}

std::string Cangjie::ProcessQuotaMarks(const std::string& value, bool processSingle)
{
    bool inDollar{false};
    auto lCurl = 0;
    auto num = 0;
    // Quotation marks that are in interpolation do not need to be escaped, like "${"abc"}".
    std::string ret;
    for (const char ch : value) {
        if (!ret.empty() && ret.back() == '$' && ch == '{') {
            inDollar = true;
        }
        if (inDollar && ch == '{') {
            lCurl++;
        }
        if (inDollar && ch == '}') {
            if (lCurl > 0) {
                lCurl--;
            }
            if (lCurl == 0) {
                inDollar = false;
            }
        }
        if (inDollar) {
            ret += ch;
            continue;
        }
        if (ch == '\"' && (ret.empty() || ret.back() != '\\')) {
            num++;
            if (num == 3) { // MultiLinesString(with 3 ") in MultiLinesString need to convert to \"\"\"
                num = 0;
                ret += "\\\"\\\"\\\"";
            }
            continue;
        }
        for (; num > 0; num--) {
            ret += processSingle ? "\\\"" : "\"";
        }
        ret += ch;
    }
    for (; num > 0; num--) {
        ret += processSingle ? "\\\"" : "\"";
    }
    return ret;
}

const std::vector<StringPart>& LexerImpl::GetStrParts(const Token& t)
{
    static const std::set<TokenKind> tkKinds = {TokenKind::STRING_LITERAL, TokenKind::MULTILINE_STRING};
    CJC_ASSERT(tkKinds.count(t.kind) != 0);

    // If the Lexer is constructed from vector<Token>, stringPartsMap will be built when
    // we see a STRING_LITERAL Token.
    if (!enableScan) {
        std::string input;
        if (t.kind == TokenKind::MULTILINE_STRING) {
#ifdef _WIN32
            input = "\"\"\"\r\n" + ProcessQuotaMarks(t.Value()) + "\"\"\"";
#else
            input = "\"\"\"\n" + ProcessQuotaMarks(t.Value()) + "\"\"\"";
#endif
        } else {
            input = "\"" + ProcessQuotaMarks(t.Value(), true) + "\"";
        }

        // We have to build StringPart using a lexer.
        LexerImpl tempLexer(
            input, diag, source, {0, t.Begin()}); // the token.pos is always zero, which is not good I suppose.
        tempLexer.Scan();
        stringPartsMap.insert(tempLexer.stringPartsMap.begin(), tempLexer.stringPartsMap.end());
    }
    CJC_ASSERT(stringPartsMap.find(t) != stringPartsMap.end() && "stringPartsMap is empty");
    return stringPartsMap[t];
}

void LexerImpl::ReadUTF8Char()
{
    pCurrent = pNext;
    if (pNext >= pInputEnd) {
        currentChar = -1;
        return;
    }
    auto ch = static_cast<int32_t>(static_cast<uint32_t>((reinterpret_cast<const uint8_t*>(pNext))[0]));
    if (ch < BYTE_2_FLAG) {
        currentChar = ch;
        pNext += (currentChar == '\r' && GetNextChar(1) == '\n') ? BYTE_2_STEP : BYTE_1_STEP;
        TryRegisterLineOffset();
        if (ch >= BYTE_X_FLAG && success) {
            diag.DiagnoseRefactor(DiagKindRefactor::lex_illegal_UTF8_encoding_byte, GetPos(pCurrent),
                ToBinaryString(static_cast<uint8_t>(*pCurrent)));
            success = false;
        }
        return;
    }
    ReadUTF8CharFromMultiBytes(ch);
    // If there is illegal utf8, eating out of all of them.
    while (ch == ERROR_UTF8 && pNext < pInputEnd) {
        ch = static_cast<int32_t>(static_cast<uint32_t>((reinterpret_cast<const uint8_t*>(pNext))[0]));
        if (ch < BYTE_X_FLAG) {
            pCurrent = pNext;
            pNext += BYTE_1_STEP;
            currentChar = ERROR_UTF8;
            return;
        }
        ReadUTF8CharFromMultiBytes(ch);
    }
    currentChar = ch;
}

void LexerImpl::ReadUTF8CharFromMultiBytes(int32_t& ch)
{
    if (!CheckArraySize(BYTE_2_STEP, ch)) {
        return;
    }
    uint32_t byte1 = static_cast<uint32_t>(ch);
    uint32_t byte2 = reinterpret_cast<const uint8_t*>(pNext)[1];
    CheckIllegalUTF8InStringLiteral(byte2);
    if (byte2 >= BYTE_2_FLAG || byte2 < BYTE_X_FLAG) {
        ch = ERROR_UTF8;
        pNext += (byte2 < BYTE_X_FLAG) ? BYTE_1_STEP : BYTE_2_STEP;
    } else if (ch >= BYTE_4_FLAG) {
        if (!CheckArraySize(BYTE_4_STEP, ch)) {
            return;
        }
        uint32_t byte3 = reinterpret_cast<const uint8_t*>(pNext)[BYTE_3_INDEX];
        uint32_t byte4 = reinterpret_cast<const uint8_t*>(pNext)[BYTE_4_INDEX];
        CheckIllegalUTF8InStringLiteral(byte3);
        CheckIllegalUTF8InStringLiteral(byte4);
        ch = int32_t(((byte1 & LOW_3_BIT_MASK) << LEFT_SHIFT_18) | ((byte2 & LOW_6_BIT_MASK) << LEFT_SHIFT_12) |
            ((byte3 & LOW_6_BIT_MASK) << LEFT_SHIFT_6) | (byte4 & LOW_6_BIT_MASK));
        CheckUnsecureUnicodeValue(ch);
        // disallow ambiguous code points for security
        if (ch < BYTE_4_BASE) {
            ch = ERROR_UTF8;
        }
        pNext += BYTE_4_STEP;
    } else if (ch >= BYTE_3_FLAG) {
        if (!CheckArraySize(BYTE_3_STEP, ch)) {
            return;
        }
        uint32_t byte3 = reinterpret_cast<const uint8_t*>(pNext)[BYTE_3_INDEX];
        CheckIllegalUTF8InStringLiteral(byte3);
        ch = int32_t(((byte1 & LOW_4_BIT_MASK) << LEFT_SHIFT_12) | ((byte2 & LOW_6_BIT_MASK) << LEFT_SHIFT_6) |
            (byte3 & LOW_6_BIT_MASK));
        CheckUnsecureUnicodeValue(ch);
        // disallow ambiguous code points for security
        if (ch < BYTE_3_BASE) {
            ch = ERROR_UTF8;
        }
        pNext += BYTE_3_STEP;
    } else {
        ch = int32_t(((byte1 & LOW_5_BIT_MASK) << LEFT_SHIFT_6) | (byte2 & LOW_6_BIT_MASK));
        CheckUnsecureUnicodeValue(ch);
        // disallow ambiguous code points for security
        if (ch < BYTE_2_BASE) {
            ch = ERROR_UTF8;
        }
        pNext += BYTE_2_STEP;
    }
}

TokenKind LexerImpl::LookupKeyword(const std::string& literal)
{
    if (LexerImpl::tokenMap.count(literal) != 0) {
        // keyword
        return LexerImpl::tokenMap[literal];
    }

    // identifier
    return TokenKind::IDENTIFIER;
}

void LexerImpl::Back()
{
    pNext = pCurrent;
}

std::string LexerImpl::GetSuffix(const char* pSuffixStart)
{
    while (std::isalnum(currentChar)) {
        ReadUTF8Char();
    }
    std::string suffix;
    if (pSuffixStart + 1 < pInputEnd) {
        if (pCurrent != pInputEnd && pSuffixStart + 1 < pNext - 1) {
            if (*(pNext - 1) == '\n' && *(pNext - BYTE_2_STEP) == '\r') {
                suffix = std::string(pSuffixStart + 1, pNext - BYTE_2_STEP);
            } else {
                suffix = std::string(pSuffixStart + 1, pNext - 1);
            }
        } else if (pCurrent == pInputEnd && pSuffixStart + 1 < pNext) {
            suffix = std::string(pSuffixStart + 1, pNext);
        }
    }
    return suffix;
}

void LexerImpl::ProcessIntegerSuffix()
{
    std::string suffixType{static_cast<char>(currentChar)};
    auto pSuffixStart = pCurrent;
    auto suffix = GetSuffix(pSuffixStart);
    if (!(suffix == "64" || suffix == "32" || suffix == "8" || suffix == "16")) {
        DiagUnexpectedIntegerLiteralTypeSuffix(pSuffixStart, suffixType, suffix);
        success = false;
        tokenKind = TokenKind::ILLEGAL;
    }
    Back();
    return;
}

const char* LexerImpl::PrefixName(const char prefix) const
{
    switch (prefix) {
        case 'x':
            return "hexadecimal";
        case 'o':
            return "octal";
        case 'b':
            return "binary";
        default:
            return "decimal";
    }
}

// Check if next char after '.' indicates a member access (.identifier) or range operator (..)
// Returns true if we should NOT parse a decimal fraction (i.e., stop before the dot)
bool LexerImpl::ShouldStopBeforeDot() const
{
    char nextChar = GetNextChar(0);
    // Range operator: .. or ..= or ...
    if (nextChar == '.') {
        return true;
    }
    // Member access: .identifier - check if next char is identifier start
    if (!std::isdigit(nextChar)) {
        return true;
    }
    return false;
}

// Scan binary digits: [01] ([01] | '_')*
// Returns true if at least one digit was scanned
bool LexerImpl::ScanBinDigits(bool& hasDigit, const char* reasonPoint)
{
    while (currentChar == '0' || currentChar == '1' || currentChar == '_') {
        if (currentChar != '_') {
            hasDigit = true;
        }
        ReadUTF8Char();
    }
    // Check for invalid digits in binary literal
    if (std::isdigit(currentChar) && success) {
        DiagUnexpectedDigit(BIN_BASE, reasonPoint);
        success = false;
        // Consume remaining invalid digits
        while (std::isdigit(currentChar) || currentChar == '_') {
            ReadUTF8Char();
        }
    }
    return hasDigit;
}

// Scan octal digits: [0-7] ([0-7] | '_')*
// Returns true if at least one digit was scanned
bool LexerImpl::ScanOctDigits(bool& hasDigit, const char* reasonPoint)
{
    while ((currentChar >= '0' && currentChar <= '7') || currentChar == '_') {
        if (currentChar != '_') {
            hasDigit = true;
        }
        ReadUTF8Char();
    }
    // Check for invalid digits in octal literal
    if (std::isdigit(currentChar) && success) {
        DiagUnexpectedDigit(OCT_BASE, reasonPoint);
        success = false;
        // Consume remaining invalid digits
        while (std::isdigit(currentChar) || currentChar == '_') {
            ReadUTF8Char();
        }
    }
    return hasDigit;
}

// Scan decimal digits: DecDigit (DecDigit | '_')* where DecDigit = [0-9]
// Returns true if at least one digit was scanned
// Also detects and reports unexpected hex digits (a-f) in decimal context
bool LexerImpl::ScanDecDigits(bool& hasDigit, const char* reasonPoint)
{
    while (std::isdigit(currentChar) || currentChar == '_') {
        if (currentChar != '_') {
            hasDigit = true;
        }
        ReadUTF8Char();
    }
    // Check for hex digits that are invalid in decimal context
    // Don't flag 'e'/'E' (exponent) or 'f' (float suffix) as errors here
    while (std::isxdigit(currentChar) && std::tolower(currentChar) != 'e' && currentChar != 'f') {
        hasDigit = true;
        if (success) {
            DiagUnexpectedDigit(DEC_BASE, reasonPoint);
            success = false;
        }
        ReadUTF8Char();
        // Continue consuming any remaining digits/hex chars
        while (std::isxdigit(currentChar) || currentChar == '_') {
            ReadUTF8Char();
        }
    }
    return hasDigit;
}

// Scan hex digits: HexDigit (HexDigit | '_')* where HexDigit = [0-9a-fA-F]
// Returns true if at least one digit was scanned
bool LexerImpl::ScanHexDigits(bool& hasDigit)
{
    while (std::isxdigit(currentChar) || currentChar == '_') {
        if (currentChar != '_') {
            hasDigit = true;
        }
        ReadUTF8Char();
    }
    return hasDigit;
}

// Scan decimal exponent: E '-'? DecFrag
// DecFrag = DecDigit (DecDigit | '_')*
void LexerImpl::ScanDecExp([[maybe_unused]] const char* reasonPoint)
{
    // Skip 'e' or 'E'
    ReadUTF8Char();
    tokenKind = TokenKind::FLOAT_LITERAL;
    if (currentChar == '-') {
        ReadUTF8Char();
    }
    // Must have at least one digit
    if (currentChar == '_' && success) {
        DiagExpectedDigit('d');
        success = false;
    }
    bool expHasDigit = false;
    // Use simple digit scanning for exponent (no hex digit checking)
    while (std::isdigit(currentChar) || currentChar == '_') {
        if (currentChar != '_') {
            expHasDigit = true;
        }
        ReadUTF8Char();
    }
    if (!expHasDigit && success) {
        DiagExpectedDigit('d');
        success = false;
    }
}

// Scan hex exponent: P '-'? DecFrag
void LexerImpl::ScanHexExp()
{
    ReadUTF8Char();
    tokenKind = TokenKind::FLOAT_LITERAL;
    if (currentChar == '-') {
        ReadUTF8Char();
    }
    // Must have at least one digit
    if (currentChar == '_' && success) {
        DiagExpectedDigit('d');
        success = false;
    }
    bool expHasDigit = false;
    // Use simple digit scanning for exponent
    while (std::isdigit(currentChar) || currentChar == '_') {
        if (currentChar != '_') {
            expHasDigit = true;
        }
        ReadUTF8Char();
    }
    if (!expHasDigit && success) {
        DiagExpectedDigit('d');
        success = false;
    }
}

// Scan binary number: '0' B [01] ([01] | '_')* IntSuffix?
Token LexerImpl::ScanBinNumber(const char* pStart, const char* reasonPoint)
{
    tokenKind = TokenKind::INTEGER_LITERAL;
    bool hasDigit = false;

    // Check for leading underscore (invalid)
    if (currentChar == '_' && success) {
        DiagExpectedDigit('b');
        success = false;
    }
    ScanBinDigits(hasDigit, reasonPoint);
    if (!hasDigit && success) {
        DiagExpectedDigit('b');
        success = false;
    }
    if (currentChar == 'i' || currentChar == 'u') {
        ProcessIntegerSuffix();
    }
    Back();
    return Token(tokenKind, std::string(pStart, pNext), pos, GetPos(pNext));
}

// Scan octal number: '0' O [0-7] ([0-7] | '_')* IntSuffix?
Token LexerImpl::ScanOctNumber(const char* pStart, const char* reasonPoint)
{
    tokenKind = TokenKind::INTEGER_LITERAL;
    bool hasDigit = false;
    // Check for leading underscore (invalid)
    if (currentChar == '_' && success) {
        DiagExpectedDigit('o');
        success = false;
    }
    ScanOctDigits(hasDigit, reasonPoint);
    if (!hasDigit && success) {
        DiagExpectedDigit('o');
        success = false;
    }
    // Check for integer suffix
    if (currentChar == 'i' || currentChar == 'u') {
        ProcessIntegerSuffix();
    }
    Back();
    return Token(tokenKind, std::string(pStart, pNext), pos, GetPos(pNext));
}

// Scan hex number or hex float:
// Hex: '0' X HexDigit (HexDigit | '_')* IntSuffix?
// HexFloat: '0' X (HexDigits | HexFrac | HexDigits HexFrac) HexExp
// HexFrac: '.' HexDigits
Token LexerImpl::ScanHexNumber(const char* pStart, const char* reasonPoint)
{
    tokenKind = TokenKind::INTEGER_LITERAL;
    bool hasDigit = false;

    // Check for hex float starting with '.' (e.g., 0x.ap02)
    if (currentChar == '.' && std::isxdigit(GetNextChar(0))) {
        tokenKind = TokenKind::FLOAT_LITERAL;
        ReadUTF8Char(); // consume '.'
        ScanHexDigits(hasDigit);
        // Hex float requires exponent part
        if (std::tolower(currentChar) == 'p') {
            ScanHexExp();
        } else if (success) {
            DiagExpectedExponentPart(reasonPoint);
            success = false;
        }
        ProcessNumberFloatSuffix('x', true);
        Back();
        return Token(tokenKind, std::string(pStart, pNext), pos, GetPos(pNext));
    }
    // Check for leading underscore (invalid)
    if (currentChar == '_' && success) {
        DiagExpectedDigit('x');
        success = false;
    }
    ScanHexDigits(hasDigit);
    // Check for hex fraction: '.' HexDigits
    // But be careful: 0x1.foo() should be hex int 0x1 followed by member access .foo()
    // We need to look ahead to see if this is really a hex float (requires 'p' exponent)
    if (currentChar == '.' && std::isxdigit(GetNextChar(0))) {
        // Save position in case we need to backtrack
        const char* dotPos = pCurrent;
        const char* afterDotNext = pNext;
        int32_t savedChar = currentChar;
        // Tentatively consume '.' and hex digits
        ReadUTF8Char(); // consume '.'
        bool fracHasDigit = false;
        ScanHexDigits(fracHasDigit);
        // Check if this is actually a hex float (must have 'p' exponent)
        // or if it's a member access (followed by identifier chars like 'o' in 'foo')
        if (std::tolower(currentChar) == 'p') {
            // It's a hex float - continue with exponent
            tokenKind = TokenKind::FLOAT_LITERAL;
        } else if (std::isalpha(currentChar) || currentChar == '_') {
            // Followed by identifier character - this is member access, backtrack
            pCurrent = dotPos;
            pNext = afterDotNext;
            currentChar = savedChar;
            // Don't set tokenKind to FLOAT_LITERAL, keep as INTEGER_LITERAL
        } else {
            // No 'p' exponent and not identifier - it's an invalid hex float
            tokenKind = TokenKind::FLOAT_LITERAL;
        }
    }

    // Hex float requires exponent part
    if (std::tolower(currentChar) == 'p') {
        ScanHexExp();
    } else if (tokenKind == TokenKind::FLOAT_LITERAL && success) {
        // Hex float without exponent is an error
        DiagExpectedExponentPart(reasonPoint);
        success = false;
    }
    if (!hasDigit && success) {
        DiagExpectedDigit('x');
        success = false;
    }
    // Check for integer suffix (only valid for integer, not float)
    if (tokenKind == TokenKind::INTEGER_LITERAL && (currentChar == 'i' || currentChar == 'u')) {
        ProcessIntegerSuffix();
    }
    // Process float suffix and edge cases
    if (tokenKind == TokenKind::FLOAT_LITERAL) {
        ProcessNumberFloatSuffix('x', true);
    }
    Back();
    return Token(tokenKind, std::string(pStart, pNext), pos, GetPos(pNext));
}

// Scan decimal number or float:
// Dec: [1-9] (DecDigit | '_')* | '0'
// Int: Dec '_'* IntSuffix?
// Float: (Dec DecExp | DecFrac DecExp? | (Dec DecFrac) DecExp?) FloatSuffix?
Token LexerImpl::ScanDecNumber(const char* pStart)
{
    tokenKind = TokenKind::INTEGER_LITERAL;
    bool hasDigit = false;
    const char* reasonPoint = pStart;

    // Scan decimal digits
    ScanDecDigits(hasDigit, reasonPoint);

    // For backward compatibility: emit diagnostic if decimal starts with 0 followed by digits
    // e.g., 0127, 000 (but not just 0)
    bool isIllegalStart = IsIllegalStartDecimalPart(pStart, pCurrent);

    // Check for decimal fraction or range/member access
    if (currentChar == '.') {
        if (ShouldStopBeforeDot()) {
            // Range operator (..) or member access (.identifier)
            // Emit backward compat diagnostic for cases like 0127..0333 or 000.a
            if (isIllegalStart) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::lex_cannot_start_with_digit, GetPos(pStart), "integer", std::string{*pStart});
            }
            Back();
            return Token(tokenKind, std::string(pStart, pNext), pos, GetPos(pNext));
        }

        // This is a decimal fraction
        tokenKind = TokenKind::FLOAT_LITERAL;
        // Emit backward compat diagnostic for cases like 000.5
        if (isIllegalStart) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::lex_cannot_start_with_digit, GetPos(pStart), "float", std::string{*pStart});
        }
        ReadUTF8Char(); // consume '.'
        bool fracHasDigit = false;
        ScanDecDigits(fracHasDigit, reasonPoint);

        if (!fracHasDigit && success) {
            DiagExpectedDigit('d');
            success = false;
        }
    } else {
        // No decimal point - emit diagnostic if needed
        if (isIllegalStart) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::lex_cannot_start_with_digit, GetPos(pStart), "integer", std::string{*pStart});
        }
    }

    // Check for exponent part: E '-'? DecFrag
    if (std::tolower(currentChar) == 'e') {
        ScanDecExp(reasonPoint);
    }
    // Check for integer suffix (only valid for integer)
    if (tokenKind == TokenKind::INTEGER_LITERAL) {
        // Allow trailing underscores before suffix per grammar: Dec '_'* IntSuffix?
        while (currentChar == '_') {
            ReadUTF8Char();
        }
        if (currentChar == 'i' || currentChar == 'u') {
            ProcessIntegerSuffix();
        }
    }

    // Process float suffix and edge cases
    bool isFloat = (tokenKind == TokenKind::FLOAT_LITERAL);
    ProcessNumberFloatSuffix('d', isFloat);
    Back();
    return Token(tokenKind, std::string(pStart, pNext), pos, GetPos(pNext));
}

// Entry point for scanning a number starting with '.'
// DecFrac: '.' DecFrag where DecFrag = DecDigit (DecDigit | '_')*
Token LexerImpl::ScanDecFracStart(const char* pStart)
{
    tokenKind = TokenKind::FLOAT_LITERAL;
    const char* reasonPoint = pStart;
    // We're at '.', consume it
    ReadUTF8Char();
    bool hasDigit = false;
    ScanDecDigits(hasDigit, reasonPoint);
    if (!hasDigit && success) {
        DiagExpectedDigit('d');
        success = false;
    }
    if (std::tolower(currentChar) == 'e') {
        ScanDecExp(reasonPoint);
    }
    ProcessNumberFloatSuffix('d', true);
    Back();
    return Token(tokenKind, std::string(pStart, pNext), pos, GetPos(pNext));
}

void LexerImpl::ProcessFloatSuffix(const char prefix)
{
    tokenKind = TokenKind::FLOAT_LITERAL; // 0f64 should be float token
    const char* pSuffixStart = pCurrent;
    if (prefix != 'd') {
        diag.DiagnoseRefactor(DiagKindRefactor::lex_illegal_non_decimal_float, GetPos(pSuffixStart));
        success = false;
        tokenKind = TokenKind::ILLEGAL;
    }
    auto suffix = GetSuffix(pSuffixStart);
    if (!(suffix == "64" || suffix == "32" || suffix == "16")) {
        DiagUnexpectedFloatLiteralTypeSuffix(pSuffixStart, suffix);
        success = false;
        tokenKind = TokenKind::ILLEGAL;
    }
}

void LexerImpl::ProcessNumberFloatSuffix(const char& prefix, bool isFloat)
{
    using namespace Unicode;
    auto tempPoint = pCurrent;
    auto hasSuffix{false};
    if (currentChar == 'f') {
        ProcessFloatSuffix(prefix);
        hasSuffix = true;
    }
    auto suffixBegin = pCurrent;
    while (std::isalnum(currentChar) || currentChar == '.' || currentChar == '_') {
        // The range .. is legal.
        if (currentChar == '.' && GetNextChar(0) == '.' && GetNextChar(1) != '.') {
            Back();
            break;
        }
        if (pNext == pInputEnd) {
            break;
        }
        // The .identifier is legal.
        auto ppNext = reinterpret_cast<const UTF8**>(&pNext);
        auto ppEnd = reinterpret_cast<const UTF8*>(pInputEnd);
        UTF32 cp;
        UTF32* input{&cp};
        if (currentChar == '.') {
            auto cur = pNext;
            auto conv = ConvertUTF8toUTF32(ppNext, ppEnd, &input, input + 1);
            if (conv == ConversionResult::OK) {
                if (IsCJXIDStart(cp)) {
                    // valid identifier after '.', this is a member access, where the member name is possibly
                    // a Unicode identifier
                    // example: 2n.0
                    pNext = cur;
                    break;
                }
                // failed to scan an identifier after '.' but still valid unicode (e.g. excessive .), continue to
                // consume. example: .1...
                currentChar = static_cast<int>(cp);
                continue;
            }
            // failed to scan a Unicode char, break to report an error
            pNext = cur;
            currentChar = static_cast<int>(cp);
            break;
        }
        pCurrent = pNext;
        auto conv = ConvertUTF8toUTF32(ppNext, ppEnd, &input, input + 1);
        if (conv != ConversionResult::OK) {
            cp = *reinterpret_cast<const UTF8*>(pNext);
        }
        currentChar = static_cast<int>(cp);
    }
    if (((!isFloat && hasSuffix) || (suffixBegin != pNext && !IsAdjacent(suffixBegin, pNext))) && success) {
        auto errPoint = isFloat ? suffixBegin : tempPoint;
        auto args = std::string{errPoint, pCurrent};
        /// the suffix is empty, add dot to prevent empty string in diag message
        if (args.empty()) {
            args = ".";
        }
        auto builder = diag.DiagnoseRefactor(
            DiagKindRefactor::lex_unknown_suffix, MakeRange(GetPos(errPoint), GetPos(pCurrent)), args);
        builder.AddMainHintArguments(args);
    }
    Back();
}

// illegal start decimal part,e.g 01、0_1
bool LexerImpl::IsIllegalStartDecimalPart(const char* pStart, const char* pEnd) const
{
    if (pStart > pEnd) {
        return false;
    }
    const char* start = pStart;
    if (*start != '0') {
        return false;
    }
    // decimal starts with 0
    start++;
    if (start > pEnd) {
        return false;
    }
    if (isdigit(*start)) {
        return true;
    }
    if (*start != '_') {
        return false;
    }

    // decimal starts with 0_
    start++;
    if (start > pEnd) {
        return false;
    }
    for (; start <= pEnd && *start == '_'; start++) {
    }
    return start <= pEnd && isdigit(*start);
}

// Main entry point for number scanning
// Matches grammar:
// Float: (Dec DecExp | DecFrac DecExp? | (Dec DecFrac) DecExp?) FloatSuffix?
//      | ('0' X (HexDigits | HexFrac | HexDigits HexFrac) HexExp)
// Int: Bin IntSuffix? | Oct IntSuffix? | Dec '_'* IntSuffix? | Hex IntSuffix?
Token LexerImpl::ScanNumber(const char* pStart)
{
    // Case 1: Starts with '.' - this is DecFrac (decimal fraction start)
    if (currentChar == '.') {
        return ScanDecFracStart(pStart);
    }
    if (currentChar == '0') {
        char nextChar = GetNextChar(0);
        if (std::tolower(nextChar) == 'x') {
            ReadUTF8Char();
            const char* reasonPoint = pCurrent;
            ReadUTF8Char();
            return ScanHexNumber(pStart, reasonPoint);
        }
        if (std::tolower(nextChar) == 'o') {
            ReadUTF8Char();
            const char* reasonPoint = pCurrent;
            ReadUTF8Char();
            return ScanOctNumber(pStart, reasonPoint);
        }

        if (std::tolower(nextChar) == 'b') {
            ReadUTF8Char();
            const char* reasonPoint = pCurrent;
            ReadUTF8Char();
            return ScanBinNumber(pStart, reasonPoint);
        }

        // Just '0' or '0' followed by more decimal digits (backward compat: 0127 style)
        // Per grammar, Dec = [1-9] (DecDigit | '_')* | '0'
        // So '0' alone is valid, but '0127' is not - however we keep it for backward compat
        return ScanDecNumber(pStart);
    }
    return ScanDecNumber(pStart);
}

Token LexerImpl::ScanDotPrefixSymbol()
{
    if (GetNextChar(0) == '.') {
        ReadUTF8Char();
        if (GetNextChar(0) == '.') {
            ReadUTF8Char();
            return Token(TokenKind::ELLIPSIS, "...", pos, GetPos(pNext));
        } else if (GetNextChar(0) == '=') {
            ReadUTF8Char();
            return Token(TokenKind::CLOSEDRANGEOP, "..=", pos, GetPos(pNext));
        } else {
            return Token(TokenKind::RANGEOP, "..", pos, GetPos(pNext));
        }
    } else {
        return Token(TokenKind::DOT, ".", pos, GetPos(pNext));
    }
}

using namespace Unicode;
bool LexerImpl::TryConsumeIdentifierUTF8Char()
{
    CJC_ASSERT(pNext != pCurrent);
    pCurrent = pNext;
    unsigned codePoint;
    unsigned* p{&codePoint};
    auto suc =
        ConvertUTF8toUTF32(reinterpret_cast<const UTF8**>(&pNext), reinterpret_cast<const UTF8*>(pInputEnd), &p, p + 1);
    if (suc != ConversionResult::OK) {
        codePoint = *reinterpret_cast<const UTF8*>(pNext++); // consume one error char
        currentChar = static_cast<int32_t>(codePoint);
        DiagIllegalUnicode();
        success = false;
        return false;
    }
    if (!IsXIDContinue(codePoint)) {
        currentChar = static_cast<int32_t>(codePoint);
        DiagIllegalUnicode();
        success = false;
        return false;
    }
    return true;
}

void LexerImpl::DiagIllegalUnicode()
{
    auto args = ConvertUnicode(currentChar);
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::lex_illegal_unicode, GetPos(pCurrent), args);
    builder.AddMainHintArguments(args);
}

void LexerImpl::ScanIdentifierContinue(Token& res, const char* pStart)
{
    CJC_ASSERT(pCurrent == pStart);
    res.kind = TokenKind::IDENTIFIER;
    while (pNext != pInputEnd) { // input may end at the last identifier
        currentChar = *pNext;
        auto cp = static_cast<UTF32>(currentChar);
        if (IsASCIIIdContinue(cp)) {
            pCurrent = pNext++;
            continue;
        }
        // pNext and pCurrent advance in call to TryConsumeIdentifierUTF8Char
        if (IsASCII(cp)) {
            break;
        }
        if (!TryConsumeIdentifierUTF8Char()) {
            res.kind = TokenKind::ILLEGAL;
            break;
        }
    }
    std::string s{pStart, pNext};
    if (res.kind == TokenKind::IDENTIFIER) {
        NFC(s);
    }
    res.SetValuePos(std::move(s), GetPos(pStart), GetPos(pNext));
}

void LexerImpl::ScanUnicodeIdentifierStart(Token& res, unsigned codePoint, const char* pStart)
{
    if (IsCJXIDStart(codePoint)) {
        return ScanIdentifierContinue(res, pStart);
    }
    if (!IsASCII(codePoint)) {
        std::string s{pStart, pCurrent};
        currentChar = static_cast<int32_t>(codePoint);
        if (IsXIDContinue(codePoint)) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::lex_unrecognized_symbol, MakeRange(GetPos(pStart), GetPos(pCurrent)), s);
        } else {
            DiagIllegalUnicode();
        }
        success = false;
        pNext = pCurrent;
        res.kind = TokenKind::ILLEGAL;
        res.SetValuePos(s, GetPos(pStart), GetPos(pNext));
        return;
    }
    CJC_ABORT();
}

void LexerImpl::ScanIdentifierOrKeyword(Token& res, const char* pStart)
{
    // starts with ascii identifier start character
    if (IsASCIIIdStart(static_cast<UTF32>(currentChar))) {
        ScanIdentifierContinue(res, pStart);
        res.kind = LookupKeyword(res.Value());
        return;
    }

    // if the first character is a valid unicode codepoint, try scan it as a unicode identifier
    UTF32 codePoint;
    auto codePointPtr{&codePoint};
    auto convSt = ConvertUTF8toUTF32(reinterpret_cast<const UTF8**>(&pStart), reinterpret_cast<const UTF8*>(pInputEnd),
        &codePointPtr, codePointPtr + 1);
    if (convSt == ConversionResult::OK) {
        ScanUnicodeIdentifierStart(res, codePoint, pCurrent);
        // no need to check for TokenKind as keyword never begins with a non-ASCII identifier
        return;
    }

    // unicode conversion failure, issue a diagnostic
    std::string s{pStart, pCurrent};
    if (IsXIDContinue(codePoint)) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::lex_unrecognized_symbol, MakeRange(GetPos(pStart), GetPos(pCurrent)), s);
    } else {
        currentChar = static_cast<int>(codePoint);
        DiagIllegalUnicode();
    }
    success = false;
    pCurrent = pNext;
    res = Token{TokenKind::ILLEGAL};
    res.SetValue(std::move(s));
}

Token LexerImpl::ScanBackquotedIdentifier(const char* pStart)
{
    Token res{TokenKind::ILLEGAL};
    std::function<bool()> scanIdentifier = [this, &res]() {
        ReadUTF8Char();
        auto codePoint = static_cast<UTF32>(currentChar);
        if (IsCJXIDStart(codePoint)) { // identifier
            ScanUnicodeIdentifierStart(res, codePoint, pCurrent);
            if (res == "_") {
                res.kind = TokenKind::WILDCARD;
                diag.DiagnoseRefactor(DiagKindRefactor::lex_expected_letter_after_underscore,
                    MakeRange(GetPos(pCurrent), GetPos(pCurrent + 1)));
            }
            // forward pCurrent, pNext and currentChar
            currentChar = *(pCurrent = pNext++);
            return true;
        }
        return false;
    };

    bool isPackageIdent{false};
    if (scanIdentifier()) {
        while (currentChar == '.' || currentChar == '-' || currentChar == ' ') {
            isPackageIdent = true;
            auto pLast = pCurrent;
            if (scanIdentifier()) {
                continue;
            }
            if (IsAdjacent(pLast, pCurrent) && success) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::lex_expected_identifier, GetPos(pCurrent), std::string{*pCurrent});
                success = false;
            }
            break;
        }
    }

    // reset identifier position to the first backquote
    res.SetValuePos("`" + res.Value(), GetPos(pStart), GetPos(pNext));
    res.kind = isPackageIdent ? TokenKind::PACKAGE_IDENTIFIER : TokenKind::IDENTIFIER;

    // scanning closing '`' after identifier
    if (currentChar == '`') {
        if (IsAdjacent(pStart, pCurrent) && success) {
            diag.DiagnoseRefactor(DiagKindRefactor::lex_expected_identifier, GetPos(pCurrent), std::string{*pCurrent});
            success = false;
        }
        // the token range exclude '`' for raw identifiers
        res.SetValuePos(res.Value() + "`", res.Begin(), res.End());
    } else {
        // error handling
        Back();
        if (!IsAdjacent(pStart, pCurrent) && success) {
            auto builder = diag.DiagnoseRefactor(
                DiagKindRefactor::lex_expected_back_quote, GetPos(pCurrent), ConvertCurrentChar());
            builder.AddHint(GetPos(pStart));
            success = false;
        } else if (success) {
            diag.DiagnoseRefactor(DiagKindRefactor::lex_expected_identifier, GetPos(pCurrent), ConvertCurrentChar());
            success = false;
        }
        while (currentChar != '`' && currentChar != ' ' && !IsCurrentCharLineTerminator() && currentChar != -1) {
            ReadUTF8Char();
        }
    }
    return res;
}

std::string LexerImpl::ConvertCurrentChar()
{
    if (IsCurrentCharLineTerminator()) {
        return "new line character";
    }
    const int32_t& ch = currentChar;
    return ConvertChar(ch);
}

void LexerImpl::ProcessUnicodeEscape()
{
    int hexNum = 0;
    UTF32 hexVal{0};
    const char* uniStart = pCurrent;
    const char* old = pCurrent - 1;
    ReadUTF8Char();
    if (currentChar != '{' && success) {
        auto builder =
            diag.DiagnoseRefactor(DiagKindRefactor::lex_expected_left_bracket, GetPos(pCurrent), ConvertCurrentChar());
        builder.AddHint(MakeRange(GetPos(pCurrent - std::string("\\u").size()), GetPos(pCurrent)));
        success = false;
        return;
    }
    for (;;) {
        ReadUTF8Char();
        if (hexNum == UNICODE_MAX_NUM) {
            Back();
            break;
        }
        if (isxdigit(currentChar)) {
            hexNum++;
            UTF32 n{0};
            constexpr UTF32 base{4}; // count to shift when converting hexademical to decimal
            constexpr UTF32 ten{10}; // base to add when converting hexademical to decimal
            // this char is already valid hex value, since '9' < 'A' < 'a', only one check is necessary
            if (currentChar <= '9') { // 0-9
                n = static_cast<UTF32>(currentChar - static_cast<int>('0'));
            } else if (currentChar >= 'a') { // a-z
                n = static_cast<UTF32>(static_cast<unsigned>(currentChar - static_cast<int>('a')) + ten);
            } else {
                n = static_cast<UTF32>(static_cast<unsigned>(currentChar - static_cast<int>('A')) + ten);
            }
            hexVal = (hexVal << base) | static_cast<UTF32>(n);
        } else {
            Back();
            break;
        }
    }
    if (hexNum == 0 && success) {
        DiagExpectedDigit('x');
        success = false;
    }
    if (hexNum == UNICODE_MAX_NUM && isxdigit(currentChar) && success) { // 1 to 8 hex digits allowed
        DiagExpectedRightBracket(uniStart - 1);
        Back();
        success = false;
    }
    ReadUTF8Char();
    if (currentChar != '}' && success) {
        if (hexNum == UNICODE_MAX_NUM) {
            DiagExpectedRightBracket(uniStart - 1);
        } else {
            DiagExpectedRightBracketOrHexadecimal(uniStart - 1);
        }
        Back();
        success = false;
    }
    if (!IsLegalUnicode(hexVal) && success) {
        std::stringstream stream;
        stream << std::hex << hexVal;
        std::string result(stream.str());
        diag.DiagnoseRefactor(
            DiagKindRefactor::lex_illegal_uni_character_literal, MakeRange(GetPos(old), GetPos(pNext)), result);
    }
}

void LexerImpl::ProcessEscape(const char* pStart, bool isInString, bool isByteLiteral)
{
    ReadUTF8Char();
    if (IsLegalEscape(currentChar) || (!isByteLiteral && static_cast<char>(currentChar) == '$')) {
        return;
    }
    if (currentChar == 'u') {
        ProcessUnicodeEscape();
    } else {
        if (success) {
            DiagUnrecognizedEscape(pStart, isInString, isByteLiteral);
            success = false;
        }
        Back();
    }
}

// Return position of arbitrary location.
Position LexerImpl::GetPos(const char* current)
{
    size_t offset = static_cast<size_t>((current >= pInputEnd ? pInputEnd : current) - pInputStart);

    size_t loc = lineOffsetsFromBase.size() - 1;
    // Find target line base from line base offset vector.
    if (offset < lineOffsetsFromBase.back()) {
        auto offsetIndex = std::upper_bound(lineOffsetsFromBase.begin(), lineOffsetsFromBase.end(), offset);
        loc = static_cast<size_t>(std::distance(lineOffsetsFromBase.begin(), offsetIndex) - 1);
    }
    // If reach to the end and last character is newline, decline last extra column.
    if (pInputStart < pInputEnd && current == pInputEnd && loc != 0) {
        if (*(current - 1) == '\n') {
            loc--;
        }
        if (current - BYTE_2_STEP >= pInputStart && *(current - BYTE_2_STEP) == '\r' && *(current - 1) == '\n') {
            offset--;
        }
    }
    // Only first line need the column base.
    int columnBase = loc == 0 ? posBase.column : 1;

    auto column = columnBase + static_cast<int>(offset - lineOffsetsFromBase[loc]);

    return Position{posBase.fileID, posBase.line + static_cast<int>(loc), static_cast<int>(column)};
}

std::pair<Token, bool> LexerImpl::ScanStringOrJString(const char* pStart, bool needStringParts, bool isJString)
{
    bool res{true};
    std::vector<StringPart> stringParts;
    const char* begin = pStart + 1;
    stringStarts.emplace_back(pStart, false);
    tokenKind = isJString ? TokenKind::JSTRING_LITERAL : TokenKind::STRING_LITERAL;
    size_t offset = 1;
    Position beginPos = GetPos(begin);

    if (isJString) {
        ReadUTF8Char();
        ++offset;
    }
    auto quote = currentChar;
    for (;;) {
        ReadUTF8Char();
        if (currentChar == quote) {
            stringParts.emplace_back(StringPart::STR, std::string(begin, pCurrent), beginPos, GetPos(pCurrent));
            break;
        } else if (IsCurrentCharLineTerminator() || (currentChar == -1)) {
            stringStarts.pop_back();
            return {ProcessIllegalToken(needStringParts, false, isJString ? pStart + 1 : pStart, isJString), false};
        } else if (currentChar == '\\') {
            ProcessEscape(pStart, true, false);
        } else if (currentChar == '$' && GetNextChar(0) == '{' && !isJString) {
            std::tie(begin, res) = ProcessStringInterpolation(begin, beginPos, stringParts);
            if (!res) {
                break;
            }
        }
    }
    auto ret = Token(tokenKind, std::string(pStart + offset, pCurrent), pos, GetPos(pNext));
    ret.isSingleQuote = IsSingleQuote(quote);
    if (needStringParts) {
        stringPartsMap.emplace(std::pair{ret, stringParts});
    }
    stringStarts.pop_back();
    return {ret, res};
}

Token LexerImpl::ProcessIllegalToken(bool needStringParts, bool multiLine, const char* pStart, bool isJString)
{
    bool isMatchEnd = currentChar == -1;
    if (success) {
        if (!multiLine) {
            DiagUnterminatedSingleLineString(pStart, isMatchEnd, isJString);
        } else {
            DiagUnterminatedMultiLineString(pStart);
        }
        success = false;
    }
    auto tokKind =
        multiLine ? TokenKind::MULTILINE_STRING : (isJString ? TokenKind::JSTRING_LITERAL : TokenKind::STRING_LITERAL);
    auto tok = Token(tokKind, std::string{pStart, pCurrent}, pos, GetPos(pCurrent));
    std::vector<StringPart> stringParts{
        StringPart{StringPart::STR, std::string(pStart, pCurrent), pos, GetPos(pCurrent)}};
    if (needStringParts) {
        stringPartsMap.emplace(std::pair{tok, stringParts});
    }
    return tok;
}

bool LexerImpl::ScanInterpolationString(const char* pStart, bool allowNewLine)
{
    interpolations.push_back(pCurrent);
    ReadUTF8Char();
    if (!ScanInterpolationStringLiteralHoleBalancedText(pStart, '}', allowNewLine)) {
        interpolations.pop_back();
        return false;
    }
    interpolations.pop_back();
    if (currentChar == '}') {
        return true;
    }
    return false;
}

bool LexerImpl::ScanInterpolationStringLiteralHoleComment(bool allowNewline)
{
    if (GetNextChar(0) == '/' && !allowNewline) {
        for (;;) {
            ReadUTF8Char();
            if (IsCurrentCharLineTerminator() || (currentChar == -1)) {
                break;
            }
        }
        if (success) {
            DiagUnterminatedInterpolation();
            success = false;
        }
        Back();
        return false;
    }
    if (GetNextChar(0) == '*' || GetNextChar(0) == '/') {
        auto [t, res] = ScanComment(pCurrent, allowNewline);
        return res;
    }
    return true;
}

bool LexerImpl::ProcessInterpolationStringLiteralLineBreak(bool allowNewline)
{
    if (IsCurrentCharLineTerminator()) {
        if (allowNewline) {
            return true;
        }
    }
    if (success) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::lex_unterminated_interpolation,
            MakeRange(GetPos(interpolations.back()), GetPos(pCurrent) + Position{0, 0, 1}));
        if (!allowNewline && currentChar != -1) {
            builder.AddHint(GetPos(stringStarts.back().first));
        }
        success = false;
    }
    return false;
}

bool LexerImpl::ScanInterpolationStringLiteralHoleBalancedTextString()
{
    CJC_ASSERT(currentChar == '\'' || currentChar == '"');
    if (GetNextChar(0) == currentChar && GetNextChar(1) == currentChar) {
        return ScanMultiLineString(pCurrent, false).second;
    }
    return ScanStringOrJString(pCurrent, false).second;
}

bool LexerImpl::ScanInterpolationStringLiteralHoleBalancedText(const char* pStart, char endingChar, bool allowNewline)
{
    for (;;) {
        ReadUTF8Char();
        if (currentChar == '"' || currentChar == '\'') {
            if (!ScanInterpolationStringLiteralHoleBalancedTextString()) {
                return false;
            }
        } else if (currentChar == '#' && GetNextChar(0) == '#') {
            if (!ScanMultiLineRawString(pCurrent).second) {
                return false;
            }
        } else if (currentChar == '{') {
            if (!ScanInterpolationStringLiteralHoleBalancedText(pStart, '}', allowNewline)) {
                return false;
            }
        } else if (currentChar == '}') {
            if (endingChar == currentChar) {
                break;
            }
        } else if (currentChar == -1 || IsCurrentCharLineTerminator()) {
            if (!ProcessInterpolationStringLiteralLineBreak(allowNewline)) {
                return false;
            }
        } else if (currentChar == '/') {
            if (!ScanInterpolationStringLiteralHoleComment(allowNewline)) {
                return false;
            }
        }
    }
    return true;
}

std::pair<const char*, bool> LexerImpl::ProcessStringInterpolation(
    const char* pStart, Position& beginPos, std::vector<StringPart>& stringParts, bool allowNewLine)
{
    const char* begin = pStart;
    // If the String Interpolation ${ is at the very beginning of the line, use allowNewLine also means
    // ScanMultiLineString.
    bool interpolationAtBegin = allowNewLine && GetPos(pCurrent).column == 1;
    if (begin != pCurrent || interpolationAtBegin) {
        stringParts.emplace_back(StringPart(StringPart::STR, std::string(begin, pCurrent), beginPos, GetPos(pCurrent)));
        begin = pCurrent;
        beginPos = GetPos(begin);
    }
    bool closeBrace = ScanInterpolationString(pStart, allowNewLine);
    if (closeBrace) {
        stringParts.emplace_back(
            StringPart(StringPart::EXPR, std::string(begin, pCurrent + 1), beginPos, GetPos(pCurrent + 1)));
        begin = pCurrent + 1;
        beginPos = GetPos(begin);
    }
    return {begin, closeBrace};
}

std::pair<Token, bool> LexerImpl::ScanMultiLineString(const char* pStart, bool needStringParts)
{
    bool res{true};
    std::vector<StringPart> stringParts;
    stringStarts.emplace_back(pCurrent, true);
    auto quote = currentChar;
    ReadUTF8Char(); // consume second " or '
    ReadUTF8Char(); // consume third " or '
    std::string beginDelimiters = R"(""")";
    size_t multiStringBeginOffset = beginDelimiters.size();
    size_t multiStringEndOffset = multiStringBeginOffset - 1; // should be 2
    uint8_t terminatorLength = Utils::GetLineTerminatorLength(pNext, pInputEnd);
    if (terminatorLength > 0) {
        multiStringBeginOffset += terminatorLength;
    } else {
        if (success) {
            auto builder =
                diag.DiagnoseRefactor(DiagKindRefactor::lex_multiline_string_start_from_newline, GetPos(pNext));
            builder.AddHint(MakeRange(GetPos(pStart), GetPos(pNext)));
            success = false;
        }
    }

    const char* begin = pStart + multiStringBeginOffset;
    Position beginPos = GetPos(begin);
    for (;;) {
        ReadUTF8Char();
        if (currentChar == '\\') {
            ProcessEscape(pStart, true, false);
        } else if ((currentChar == quote) && (GetNextChar(0) == quote) && (GetNextChar(1) == quote)) {
            stringParts.emplace_back(StringPart::STR, std::string(begin, pCurrent), beginPos, GetPos(pCurrent));
            ReadUTF8Char();
            ReadUTF8Char();
            break;
        } else if (currentChar == -1) {
            stringStarts.pop_back();
            return {ProcessIllegalToken(needStringParts, true, pStart), false};
        } else if (currentChar == '$' && GetNextChar(0) == '{') {
            std::tie(begin, res) = ProcessStringInterpolation(begin, beginPos, stringParts, true);
        }
    }
    auto ret = Token(TokenKind::MULTILINE_STRING,
        std::string(pStart + multiStringBeginOffset, pCurrent - multiStringEndOffset), pos,
        GetPos(pNext)); // """ is not token value
    ret.isSingleQuote = IsSingleQuote(quote);
    if (needStringParts) {
        stringPartsMap.emplace(std::pair{ret, stringParts});
    }
    stringStarts.pop_back();
    return {ret, res};
}

void LexerImpl::ConsumeNChar(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        ReadUTF8Char();
    }
}

std::pair<Token, bool> LexerImpl::ScanMultiLineRawString(const char* pStart)
{
    uint32_t delimiterNum = 0;
    while (currentChar == '#') {
        delimiterNum++;
        ReadUTF8Char();
    }
    if (currentChar != '"' && currentChar != '\'' && success) {
        auto builder = diag.DiagnoseRefactor(
            DiagKindRefactor::lex_expected_quote_in_raw_string, GetPos(pCurrent), ConvertCurrentChar());
        builder.AddHint(MakeRange(GetPos(pStart), GetPos(pCurrent)));
        success = false;
        return {Token(TokenKind::MULTILINE_RAW_STRING, "", pos, GetPos(pCurrent)), false};
    }
    auto quote = currentChar;
    uint32_t count = delimiterNum;
    for (;;) {
        ReadUTF8Char();
        if ((currentChar == quote) && (GetNextChar(0) == '#')) {
            const char* pTmp = pNext;
            while (pTmp[0] == '#' && count > 0) {
                count--;
                pTmp++;
            }
            if (count == 0) {
                ConsumeNChar(delimiterNum);
                break;
            }
            count = delimiterNum;
        } else if (currentChar == -1) {
            if (success) {
                DiagUnterminatedRawString(pStart);
                success = false;
            }
            return {Token(TokenKind::MULTILINE_RAW_STRING, "", pos, GetPos(pNext)), false};
        }
    }
    auto tok = Token(TokenKind::MULTILINE_RAW_STRING, std::string(pStart + delimiterNum + 1, pCurrent - delimiterNum),
        pos, GetPos(pNext)); // Delimiters are not token value, but included in position range begin..end.
    tok.delimiterNum = delimiterNum;
    tok.isSingleQuote = IsSingleQuote(quote);
    return {tok, false};
}

std::pair<Token, bool> LexerImpl::ScanMultiLineComment(const char* pStart, bool allowNewLine)
{
    size_t level = 1;
    while ((currentChar != -1) && (level > 0)) {
        ReadUTF8Char();
        if (IsCurrentCharLineTerminator()) {
            if (success && !allowNewLine) {
                DiagUnterminatedInterpolation();
                success = false;
                break;
            }
        }
        if (currentChar == '*' && GetNextChar(0) == '/') {
            level -= 1;
            ReadUTF8Char();
        } else if (currentChar == '/' && GetNextChar(0) == '*') {
            level += 1;
            ReadUTF8Char();
        }
    }
    if (level > 0 && success) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::lex_unterminated_block_comment, MakeRange(GetPos(pStart), GetPos(pCurrent)));
        success = false;
        return {Token(TokenKind::COMMENT, std::string(pStart, pNext), pos, GetPos(pNext)), false};
    }
    return {Token(TokenKind::COMMENT, std::string(pStart, pNext), pos, GetPos(pNext)), true};
}

std::pair<Token, bool> LexerImpl::ScanComment(const char* pStart, bool allowNewLine)
{
    ReadUTF8Char();
    if (currentChar == '*') {
        return ScanMultiLineComment(pStart, allowNewLine);
    } else {
        while ((currentChar != -1) && !IsCurrentCharLineTerminator()) {
            ReadUTF8Char();
        }
        if (IsCurrentCharLineTerminator()) {
            Back();
        }
        return {Token(TokenKind::COMMENT, std::string(pStart, pNext), pos, GetPos(pNext)), true};
    }
}

void LexerImpl::ScanCharDiagUnterminated(const char* pStart)
{
    if (success) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::lex_unterminated_char_literal, MakeRange(GetPos(pStart), GetPos(pCurrent) + 1));
        success = false;
    }
}

std::pair<Token, bool> LexerImpl::ScanChar(const char* pStart, bool isByteLiteral)
{
    /* part1: process char value between left `r'`(or `r"`) and right `'` (or '"') */
    ReadUTF8Char();
    auto quote = currentChar;
    // Skip 2 chars, `r` and (`'` or `"`)
    constexpr size_t beginOffset = 2;
    ReadUTF8Char();
    if (currentChar == quote || IsCurrentCharLineTerminator()) {
        return ProcessIllegalCharValue(pStart, beginOffset);
    }

    if (currentChar == '\\') {
        ProcessEscape(pStart, false, isByteLiteral);
    }

    /* part2: process and expect right `'` (or `"`) */
    ReadUTF8Char();
    bool isScanRightSingleQuotationSuccess = true;
    if (currentChar != quote) {
        isScanRightSingleQuotationSuccess = ProcessIllegalRightQuotation(pStart, quote);
    }
    auto tok = Token(TokenKind::RUNE_LITERAL, std::string(pStart + beginOffset, pCurrent), pos, GetPos(pNext));
    tok.isSingleQuote = IsSingleQuote(quote);
    return {tok, isScanRightSingleQuotationSuccess};
}

bool LexerImpl::ProcessIllegalRightQuotation(const char* pStart, int32_t quote)
{
    // can't find right `'` or `"`, following code is error handling.
    while (currentChar != quote && !IsCurrentCharLineTerminator() && currentChar != -1) {
        ReadUTF8Char();
    }
    if (currentChar != quote) {
        // encounter `line terminator` or `end of file`
        ScanCharDiagUnterminated(pStart);
        return false;
    } else if (success) {
        DiagCharactersOverflow(pStart);
        success = false;
    }
    return true;
}

std::pair<Token, bool> LexerImpl::ProcessIllegalCharValue(const char* pStart, size_t beginOffset)
{
    // currentChar must be one of '\'' , '\r' , '\n'
    CJC_ASSERT(currentChar == '\'' || IsCurrentCharLineTerminator());
    if (currentChar == '\'') {
        // unexpected `'`
        if (success) {
            diag.DiagnoseRefactor(DiagKindRefactor::lex_expected_character_in_char_literal,
                MakeRange(GetPos(pStart), GetPos(pCurrent + 1)));
            success = false;
        }
        return {
            Token{TokenKind::RUNE_LITERAL, std::string(pStart + beginOffset, pCurrent), pos, GetPos(pCurrent)}, true};
    }
    if (success) {
        diag.DiagnoseRefactor(DiagKindRefactor::lex_unterminated_char_literal,
            MakeRange(GetPos(pStart), GetPos(pCurrent) + Position{0, 0, 1}));
        success = false;
    }
    return {Token{TokenKind::RUNE_LITERAL, std::string(pStart + beginOffset, pCurrent), pos, GetPos(pCurrent)}, false};
}

void LexerImpl::ScanSymbolPlus()
{
    ReadUTF8Char();
    if (currentChar == '+') {
        return;
    } else if (currentChar == '&') {
        ReadUTF8Char();
        if (currentChar == '=') {
            return;
        }
    } else if (currentChar == '=') {
        return;
    }
    Back();
}

void LexerImpl::ScanSymbolHyphen()
{
    ReadUTF8Char();
    if (currentChar == '-') {
        return;
    } else if (currentChar == '=') {
        return;
    } else if (currentChar == '&') {
        ReadUTF8Char();
        if (currentChar == '=') {
            return;
        }
    } else if (currentChar == '>') {
        return;
    }
    Back();
}

void LexerImpl::ScanSymbolAsterisk()
{
    ReadUTF8Char();
    if (currentChar == '*') {
        ReadUTF8Char();
        if (currentChar == '&') {
            ReadUTF8Char();
            if (currentChar == '=') {
                return;
            }
        } else if (currentChar == '=') {
            return;
        }
    } else if (currentChar == '=') {
        return;
    } else if (currentChar == '&') {
        ReadUTF8Char();
        if (currentChar == '=') {
            return;
        }
    }
    Back();
}

void LexerImpl::ScanSymbolAmpersand()
{
    ReadUTF8Char();
    if (currentChar == '&') {
        ReadUTF8Char();
        if (currentChar == '=') {
            return;
        }
    } else if (currentChar == '=') {
        return;
    }
    Back();
}

void LexerImpl::ScanSymbolExclamation()
{
    /// Lexer read a stream of token and classify them into some syntactic
    /// category.
    constexpr int nextIndex = 2;
    if (GetNextChar(0) == 'i' && GetNextChar(1) == 'n' &&
        (GetNextChar(nextIndex) == ' ' || GetNextChar(nextIndex) == '\r' ||
            GetNextChar(nextIndex) == '\n')) { // process '!in' token
        ReadUTF8Char();
        ReadUTF8Char();
        return;
    }
    ReadUTF8Char();
    if (currentChar == '=') {
        return;
    }
    Back();
}

void LexerImpl::ScanSymbolLessThan()
{
    ReadUTF8Char();
    if (currentChar == '<') {
        ReadUTF8Char();
        if (currentChar == '=') {
            return;
        }
    } else if (currentChar == '=') {
        return;
    } else if (currentChar == ':') {
        return;
    } else if (currentChar == '-') {
        return;
    }
    Back();
}

void LexerImpl::ScanSymbolGreaterThan()
{
    if (splitAmbiguousToken) {
        return;
    }
    ReadUTF8Char();
    if (currentChar == '>') {
        ReadUTF8Char();
        if (currentChar == '=') {
            return;
        }
    } else if (currentChar == '=') {
        return;
    }
    Back();
}

void LexerImpl::ScanSymbolAt()
{
    ReadUTF8Char();
    if (currentChar == '!') {
        return;
    }
    Back();
}

void LexerImpl::ScanSymbolEqual()
{
    ReadUTF8Char();
    if (currentChar == '=') {
        return;
    } else if (currentChar == '>') {
        return;
    }
    Back();
}

void LexerImpl::ScanSymbolPercentSign()
{
    ReadUTF8Char();
    if (currentChar == '=') {
        return;
    }
    Back();
}

void LexerImpl::ScanSymbolVerticalBar()
{
    ReadUTF8Char();
    if (currentChar == '|') {
        ReadUTF8Char();
        if (currentChar == '=') {
            return;
        }
    } else if (currentChar == '=') {
        return;
    } else if (currentChar == '>') {
        return;
    }
    Back();
}

void LexerImpl::ScanSymbolTilde()
{
    ReadUTF8Char();
    if (currentChar != '>') {
        Back();
    }
}

void LexerImpl::ScanSymbolCaret()
{
    if (GetNextChar(0) == '=') {
        ReadUTF8Char();
    }
}

void LexerImpl::ScanSymbolQuest()
{
    if (splitAmbiguousToken) {
        return;
    }
    if (GetNextChar(0) == '?') {
        ReadUTF8Char();
    }
}
 
void LexerImpl::ScanSymbolColon()
{
    ReadUTF8Char();
    if (currentChar != ':') {
        Back();
    }
}

Token LexerImpl::GetSymbolToken(const char* pStart)
{
    TokenKind kind = LookupKeyword(std::string(pStart, pNext));
    if (kind == TokenKind::IDENTIFIER) {
        if (success) {
            DiagUnknownStartOfToken(GetPos(pStart));
            success = false;
        }
        return Token(TokenKind::ILLEGAL, std::string(pStart, pNext), pos, GetPos(pNext));
    }
    return Token(kind, std::string(pStart, pNext), pos, GetPos(pNext));
}

Token LexerImpl::ScanSymbol(const char* pStart)
{
    static const std::unordered_map<char, void (LexerImpl::*)()> SCAN_SYMBOL_STRATEGY = {
        {'~', &LexerImpl::ScanSymbolTilde},
        {'+', &LexerImpl::ScanSymbolPlus},
        {'-', &LexerImpl::ScanSymbolHyphen},
        {'*', &LexerImpl::ScanSymbolAsterisk},
        {'&', &LexerImpl::ScanSymbolAmpersand},
        {'^', &LexerImpl::ScanSymbolCaret},
        {'!', &LexerImpl::ScanSymbolExclamation},
        {'<', &LexerImpl::ScanSymbolLessThan},
        {'>', &LexerImpl::ScanSymbolGreaterThan},
        {'=', &LexerImpl::ScanSymbolEqual},
        {'%', &LexerImpl::ScanSymbolPercentSign},
        {'|', &LexerImpl::ScanSymbolVerticalBar},
        {'?', &LexerImpl::ScanSymbolQuest},
        {'@', &LexerImpl::ScanSymbolAt},
        {':', &LexerImpl::ScanSymbolColon},
    };
    if (currentChar == '$') {
        ReadUTF8Char();
        Token tok(TokenKind::IDENTIFIER);
        // Identifier or keyword.
        UTF32 uch = static_cast<UTF32>(currentChar);
        if (IsCJXIDStart(uch)) { // unicode identifier begin
            ScanIdentifierOrKeyword(tok, pStart + 1);
            if (tok.kind != TokenKind::IDENTIFIER) {
                DiagUnexpectedDollarIdentifier(tok);
            }
            return Token(TokenKind::DOLLAR_IDENTIFIER, "$" + tok.Value(), pos, GetPos(pNext));
        }
        if (currentChar == '`') {
            tok = ScanBackquotedIdentifier(pStart + 1);
            return Token(TokenKind::DOLLAR_IDENTIFIER, "$" + tok.Value(), pos, GetPos(pNext));
        }
        Back();
    } else if (auto iter = SCAN_SYMBOL_STRATEGY.find(static_cast<char>(currentChar));
               iter != SCAN_SYMBOL_STRATEGY.end()) {
        (this->*(iter->second))();
    }
    return GetSymbolToken(pStart);
}

Token LexerImpl::ScanFromTokens()
{
    if (curToken >= tokens.size()) {
        // If empty, return END with initialized pos.
        return Token(TokenKind::END, "", pos, pos);
    }
    if (splitAmbiguousToken) {
        auto itAmb = ambiCombinedTokensDegTable.find(tokens[curToken].kind);
        if (itAmb != ambiCombinedTokensDegTable.end()) {
            const auto& [lkind, lval, rkind, rval] = itAmb->second;
            tokens[curToken].kind = rkind;
            auto curPos = tokens[curToken].Begin();
            auto endPos = tokens[curToken].End();
            tokens[curToken].SetValuePos(std::string{rval}, curPos + 1, endPos + 1);
            return {lkind, std::string{lval}, curPos, endPos};
        }
    }
    Token t = tokens[curToken];
    curToken++;
    return t;
}

Token LexerImpl::ScanIllegalSymbol(const char* pStart)
{
    if (success) {
        DiagIllegalSymbol(pStart);
        success = false;
    }
    return Token(TokenKind::ILLEGAL, std::string(pStart, pNext), pos, GetPos(pNext));
}

Token LexerImpl::ScanByteUInt8(const char* pStart)
{
    static const size_t intByteGapLen = 3; // `\u{**}`中`{`和`}`的最大下标距离
    auto token = ScanChar(pStart, true).first;
    // check number of digits in \u
    auto position = token.Value().find("\\u", 0);
    if (position != std::string::npos) {
        auto lCurl = token.Value().find_first_of('{', 0);
        auto rCurl = token.Value().find_first_of('}', 0);
        // \u{00} position of { is 2, } is 5, 5 - 2 = 3, so the gap is at most 3
        if (lCurl != std::string::npos && rCurl != std::string::npos && rCurl - lCurl > intByteGapLen && success) {
            diag.DiagnoseRefactor(DiagKindRefactor::lex_too_many_digits, GetPos(pStart + intByteGapLen), token.Value());
            success = false;
        }
    }
    auto wString = UTF8ToChar32(token.Value());
    if (!wString.empty() && wString[0] > ASCII_BASE && success) {
        DiagUnrecognizedCharInByte(static_cast<int32_t>(wString[0]), "character byte literal", pStart,
            {GetPos(pStart + std::string("b'").size()), GetPos(pCurrent)});
        success = false;
    }
    token.SetValue("b\'" + token.Value() + "\'");
    token.kind = TokenKind::RUNE_BYTE_LITERAL;
    return token;
}

void LexerImpl::TryRegisterLineOffset()
{
    // establishing the mapping between line number and offset
    size_t terminateOffset = 0;
    if (IsCurrentCharLineTerminator()) {
        terminateOffset = static_cast<size_t>(pNext - pInputStart);
    }
    // avoid to register offsets on same point.
    if (terminateOffset > lineOffsetsFromBase.back()) {
        lineOffsetsFromBase.emplace_back(terminateOffset);
    }
}

bool LexerImpl::IsCharOrString() const
{
    return (currentChar == 'b' && GetNextChar(0) == '\'') ||
        (currentChar == 'r' && (GetNextChar(0) == '\'' || GetNextChar(0) == '\"')) || currentChar == '#' ||
        currentChar == '\'' || currentChar == '"';
}

Token LexerImpl::ScanCharOrString(const char* pStart)
{
    if (currentChar == 'b' && GetNextChar(0) == '\'') {
        return ScanByteUInt8(pStart);
    }
    if (currentChar == 'r' && (GetNextChar(0) == '\'' || GetNextChar(0) == '\"')) {
        return ScanChar(pStart).first;
    }
    if (currentChar == '#') {
        return ScanMultiLineRawString(pStart).first;
    }
    if (currentChar == '\'' || currentChar == '"') {
        return ScanSingleOrMultiLineString(pStart);
    }
    return ScanSymbol(pStart);
}

Token LexerImpl::ScanBase()
{
    const char* pStart = pNext; // pStart record the first position of a token.
    success = true;
    ReadUTF8Char();
    pos = GetPos(pStart);
    if (currentChar == -1) {
        return Token(TokenKind::END, "", pos, pos);
    }
    if (currentChar == '\n') {
        return Token(TokenKind::NL, "\n", pos, pos + 1);
    }
    if (Utils::GetLineTerminatorLength(pCurrent, pInputEnd) == Utils::WINDOWS_LINE_TERMINATOR_LENGTH) {
        return Token(TokenKind::NL, "\r\n", pos, pos + 1);
    }
    if (IsCharOrString()) {
        return ScanCharOrString(pStart);
    }
    if (currentChar == 'J' && GetNextChar(0) == '\"') {
        return ScanStringOrJString(pStart, false, true).first;
    }
    // identifier or keyword
    if (IsCJXIDStart(static_cast<UTF32>(currentChar))) {
        Token c{TokenKind::IDENTIFIER};
        ScanIdentifierOrKeyword(c, pStart);
        return c;
    }
    if (currentChar == '`') {
        return ScanBackquotedIdentifier(pStart);
    }
    if (isdigit(currentChar) || currentChar == '.') { // number
        return ScanNumberOrDotPrefixSymbol(pStart);
    }
    if (currentChar == '/') {
        return ScanDivOrComment(pStart);
    }
    if (currentChar == '\\' && IsMacroEscape(GetNextChar(0))) {
        return Token(TokenKind::ILLEGAL, "\\", pos, pos + 1);
    }
    return ScanSymbol(pStart);
}

/// Next function: return the next token
/// GetNextChar(0): the next char
/// pStart: the first char position of the current token;
/// pCurrent: first position of current char.
Token LexerImpl::Scan()
{
    Token ret{TokenKind::INTEGER_LITERAL, "", Position(0, 1, 1), Position{0, 1, 1}};
    if (!enableScan) {
        ret = ScanFromTokens();
        if (enableCollectTokenStream) {
            tokenStream.emplace(ret);
        }
        return ret;
    }
    if (pInputStart >= pInputEnd) {
        ret = Token(TokenKind::END, "", posBase, posBase);
    } else if (pNext >= pInputEnd) {
        ret = Token(TokenKind::END, "", GetPos(pNext), GetPos(pNext));
    } else {
        char next;
        while ((next = GetNextChar(0)) == ' ' || next == '\t' || next == '\f') {
            pNext++;
        }
        ret = ScanBase();
    }
    if (enableCollectTokenStream) {
        tokenStream.emplace(ret);
    }
    return ret;
}

Token LexerImpl::ScanNumberOrDotPrefixSymbol(const char* pStart)
{
    if (currentChar == '.' && (!isdigit(GetNextChar(0)))) {
        return ScanDotPrefixSymbol();
    }
    return ScanNumber(pStart);
}

Token LexerImpl::ScanSingleOrMultiLineString(const char* pStart)
{
    success = true;
    CJC_ASSERT(currentChar == '\'' || currentChar == '"');
    if (GetNextChar(0) == currentChar && GetNextChar(1) == currentChar) {
        return ScanMultiLineString(pStart).first;
    }
    return ScanStringOrJString(pStart).first;
}

Token LexerImpl::ScanDivOrComment(const char* pStart)
{
    if (GetNextChar(0) != '/' && GetNextChar(0) != '*') {
        if (GetNextChar(0) == '=') {
            ReadUTF8Char();
            return Token(TokenKind::DIV_ASSIGN, "/=", pos, pos + std::string_view{"/="}.size());
        } else {
            return Token(TokenKind::DIV, "/", pos, pos + 1);
        }
    }
    Token tok = ScanComment(pStart).first;
    // Save the comments content and their positions.
    comments.emplace_back(tok);
    return tok;
}

void LexerImpl::SetResetPoint()
{
    pResetCurrent = pCurrent;
    pResetNext = pNext;
    resetLookAheadCache = lookAheadCache;
    lineResetOffsetsFromBase = static_cast<unsigned>(lineOffsetsFromBase.size());
    resetToken = curToken;
}

void LexerImpl::Reset()
{
    pCurrent = pResetCurrent;
    pNext = pResetNext;
    lookAheadCache = resetLookAheadCache;
    curToken = resetToken;

    unsigned curLineOffsetsFromBase = static_cast<unsigned>(lineOffsetsFromBase.size());
    while (curLineOffsetsFromBase > lineResetOffsetsFromBase && !lineOffsetsFromBase.empty()) {
        lineOffsetsFromBase.pop_back();
        --curLineOffsetsFromBase;
    }
}

Token LexerImpl::Next()
{
    Token token{TokenKind::SENTINEL, "", Position(0, 1, 1), Position{0, 1, 1}};
    if (lookAheadCache.empty()) {
        token = Scan();
    } else {
        token = lookAheadCache.front();
        lookAheadCache.pop_front();
    }
    CollectToken(token);
    return token;
}

const std::list<Token>& LexerImpl::LookAhead(size_t num)
{
    if (num <= lookAheadCache.size()) {
        return lookAheadCache;
    }
    while (lookAheadCache.size() < num) {
        Token token = Scan();
        if (token.kind != TokenKind::COMMENT) { // Skip comments.
            lookAheadCache.push_back(token);
        }
        if (token.kind == TokenKind::END) {
            break;
        }
    }
    return lookAheadCache;
}

/** reserved num Tokens,
 * by default, TokenKind::COMMENT is ignored
 * additionally, e.g skipNewline == false, to make sure num is small or equal than lookAheadCache.size()
 * if Scan reaching the end of file, lookAheadCache will pad TokenKind::END(s) and eventually num ==
 * lookAheadCache.size()
 */
void LexerImpl::ReserveToken(size_t num, bool skipNewline)
{
    size_t index = 0;
    if (skipNewline) {
        for (auto it = lookAheadCache.begin(); it != lookAheadCache.end() && index < num; it++) {
            index += static_cast<size_t>(it->kind != TokenKind::NL);
        }
    } else {
        index = lookAheadCache.size();
    }

    while (index < num) {
        Token token = Scan();
        if (token.kind == TokenKind::COMMENT) { // Skip comments.
            continue;
        }
        lookAheadCache.push_back(token);
        index += static_cast<size_t>(!skipNewline || token.kind != TokenKind::NL);
    }
}

bool LexerImpl::Seeing(const std::vector<TokenKind>::const_iterator& begin,
    const std::vector<TokenKind>::const_iterator& end, bool skipNewline)
{
    CJC_ASSERT(begin <= end);
    size_t kindSize = static_cast<size_t>(std::distance(begin, end));
    ReserveToken(kindSize + 1, skipNewline);
    if (!skipNewline) {
        return std::equal(begin, end, lookAheadCache.begin(),
            [](const TokenKind kind, const Token& token) { return kind == token.kind; });
    }

    // compare two container
    auto kindIter = begin;

    // ReserveToken will ensure lookAheadCache will never smaller than std::distance(begin, end)
    // so we needn't check lookAheadIter != lookAheadCache.end()
    for (auto lookAheadIter = lookAheadCache.begin(); kindIter != end; lookAheadIter++) {
        if (lookAheadIter->kind == TokenKind::NL) {
            continue;
        }
        if (*kindIter != lookAheadIter->kind) {
            return false;
        }
        kindIter++;
    }
    return true;
}

bool LexerImpl::Seeing(const std::vector<TokenKind>& kinds, bool skipNewline)
{
    return Seeing(kinds.begin(), kinds.end(), skipNewline);
}

std::list<Token> LexerImpl::LookAheadSkipNL(size_t num)
{
    std::list<Token> ret;
    for (const Token& token : lookAheadCache) {
        if (token.kind != TokenKind::NL) {
            ret.push_back(token);
        }
        if (ret.size() >= num) {
            return ret;
        }
    }
    while (ret.size() < num) {
        Token token = Scan();
        if (token.kind == TokenKind::COMMENT) {
            continue; // Skip comments.
        }
        lookAheadCache.push_back(token);
        if (token.kind != TokenKind::NL) {
            ret.push_back(token);
        }
        if (token.kind == TokenKind::END) {
            break;
        }
    }
    return ret;
}

std::vector<Token> LexerImpl::GetTokens()
{
    std::vector<Token> tks;
    auto tk = Next();
    while (tk.kind != TokenKind::END) {
        tks.emplace_back(tk);
        tk = Next();
    }
    return tks;
}

std::vector<Token> LexerImpl::GetCollectTokens() const
{
    return collectTokens;
}

bool LexerImpl::StartCollectTokens()
{
    if (enableCollect) {
        return false;
    }
    enableCollect = true;
    collectTokens.clear();
    return true;
}

void LexerImpl::StopCollectTokens(bool bStart)
{
    if (bStart) {
        enableCollect = false;
    }
}

void LexerImpl::CollectToken(const Token& token)
{
    if (!enableCollect) {
        return;
    }
    auto it = std::find_if(collectTokens.begin(), collectTokens.end(), [&token](auto& t) { return t == token; });
    if (it == collectTokens.end()) {
        collectTokens.push_back(token);
    }
}

inline void LexerImpl::DiagSmallExpectedDigit(const bool& hasDigit, const char base)
{
    if (!hasDigit && success) {
        DiagExpectedDigit(base);
        success = false;
    }
}

inline void LexerImpl::CheckIllegalUTF8InStringLiteral(uint32_t byte)
{
    if ((byte >= BYTE_2_FLAG || byte < BYTE_X_FLAG) && success) {
        diag.DiagnoseRefactor(DiagKindRefactor::lex_illegal_UTF8_encoding_byte, GetPos(pCurrent),
            ToBinaryString(static_cast<uint8_t>(byte)));
        success = false;
    }
}
std::size_t LexerImpl::GetCurrentToken() const
{
    auto tokenNumber = this->curToken;
    for (auto token : this->lookAheadCache) {
        if (token.kind != TokenKind::SENTINEL && token.kind != TokenKind::ILLEGAL && token.kind != TokenKind::END) {
            tokenNumber--;
        }
    }
    return tokenNumber;
}
