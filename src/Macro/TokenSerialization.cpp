// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/*
 * @file
 *
 * This file implements the Cangjie Token Serialization.
 */

#include "cangjie/Macro/TokenSerialization.h"

#include <limits>
#include <securec.h>

#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/SourceManager.h"
#include "cangjie/Macro/MacroCommon.h"

using namespace TokenSerialization;

namespace {
std::string GetStringFromBytes(const uint8_t* pBuffer, uint32_t strLen)
{
    if (pBuffer == nullptr || strLen == 0) {
        return "";
    }
    // Pre-allocate capacity to avoid reallocations
    std::string value;
    value.reserve(strLen + strLen / 4); // Reserve extra space for potential "\\0" expansions
    
    for (uint32_t i = 0; i < strLen; i++) {
        if (pBuffer[i] == '\0') {
            value += "\\0";
        } else {
            value.push_back(static_cast<char>(pBuffer[i]));
        }
    }
    return value;
}
} // namespace

/**
 * Encoding tokens in memory like this.
 *
 * -> uint32_t   [uint16_t   uint32_t   char+   uint32_t   int32_t   int32_t]+
 *    ~~~~~~~~    ~~~~~~~~   ~~~~~~~~   ~~~~~   ~~~~~~~~   ~~~~~~~   ~~~~~~~ ~
 *    |           |          |          |       |          |         |       |
 *    a           b          c          d       e          f         g       h
 *
 * a: size of tokens
 * b: token kind as number
 * c: size of token value
 * d: token value as char stream
 * e: fileID as number
 * f: line number
 * g: column number
 * h: iterate each token in tokens
 *
 * @param expr : QuoteExpr desugared.
 */
std::vector<uint8_t> TokenSerialization::GetTokensBytes(const std::vector<Token>& tokens)
{
    if (tokens.empty()) {
        return {};
    }
    
    // Estimate capacity: header + per-token overhead + average string length
    // Each token: kind(2) + strLen(4) + value + fileID(4) + line(4) + column(4) + isSingle(2) + optional delimiterNum(2)
    // Estimate ~50 bytes per token on average
    size_t estimatedSize = sizeof(uint32_t) + tokens.size() * 50;
    std::vector<uint8_t> tokensBytes;
    tokensBytes.reserve(estimatedSize);
    
    auto pushBytes = [&tokensBytes](const uint8_t* v, const size_t l) {
        tokensBytes.insert(tokensBytes.end(), v, v + l);
    };
    
    uint32_t numberOfTokens = static_cast<uint32_t>(tokens.size());
    pushBytes(reinterpret_cast<const uint8_t*>(&numberOfTokens), sizeof(uint32_t));
    
    // Cache escape token kinds to avoid repeated lookups
    const auto& escapes = GetEscapeTokenKinds();
    
    for (const auto& tk : tokens) {
        uint16_t kind = static_cast<uint16_t>(tk.kind);
        pushBytes(reinterpret_cast<const uint8_t*>(&kind), sizeof(uint16_t));
        
        // use uint32_t(4 Bytes) to encode the length of string.
        const auto& value = tk.Value(); // Cache to avoid repeated calls
        uint32_t strLen = static_cast<uint32_t>(value.size());
        pushBytes(reinterpret_cast<const uint8_t*>(&strLen), sizeof(uint32_t));
        tokensBytes.insert(tokensBytes.end(), value.begin(), value.end());
        
        auto begin = tk.Begin();
        pushBytes(reinterpret_cast<const uint8_t*>(&(begin.fileID)), sizeof(uint32_t));
        pushBytes(reinterpret_cast<const uint8_t*>(&(begin.line)), sizeof(int32_t));
        
        int32_t column = begin.column;
        if (std::find(escapes.begin(), escapes.end(), tk.kind) != escapes.end() &&
            column + 1 + static_cast<int>(strLen) == tk.End().column) {
            ++column;
        }
        pushBytes(reinterpret_cast<const uint8_t*>(&column), sizeof(int32_t));
        
        uint16_t isSingleQuote = tk.isSingleQuote ? 1 : 0;
        pushBytes(reinterpret_cast<const uint8_t*>(&isSingleQuote), sizeof(uint16_t));
        
        if (tk.kind == TokenKind::MULTILINE_RAW_STRING) {
            pushBytes(reinterpret_cast<const uint8_t*>(&(tk.delimiterNum)), sizeof(uint16_t));
        }
    }
    return tokensBytes;
}

std::vector<Token> TokenSerialization::GetTokensFromBytes(const uint8_t* pBuffer)
{
    if (pBuffer == nullptr) {
        return {};
    }
    
    // Read number of tokens
    uint32_t numberOfTokens = 0;
    if (memcpy_s(&numberOfTokens, sizeof(uint32_t), pBuffer, sizeof(uint32_t)) != EOK) {
        return {};
    }
    pBuffer += sizeof(uint32_t);
    
    if (numberOfTokens == 0) {
        return {};
    }
    
    // Pre-allocate capacity to avoid reallocations
    std::vector<Token> tokens;
    tokens.reserve(numberOfTokens);
    
    constexpr auto i4 = sizeof(int32_t);
    
    for (uint32_t i = 0; i < numberOfTokens; ++i) {
        uint16_t kind = 0;
        if (memcpy_s(&kind, sizeof(uint16_t), pBuffer, sizeof(uint16_t)) != EOK) {
            return {};
        }
        pBuffer += sizeof(uint16_t);
        
        uint32_t strLen = 0;
        if (memcpy_s(&strLen, sizeof(uint32_t), pBuffer, sizeof(uint32_t)) != EOK) {
            return {};
        }
        pBuffer += sizeof(uint32_t);
        
        std::string value = GetStringFromBytes(pBuffer, strLen);
        pBuffer += strLen;
        
        uint32_t fileID = 0;
        if (memcpy_s(&fileID, sizeof(uint32_t), pBuffer, i4) != EOK) {
            return {};
        }
        pBuffer += i4;
        
        int32_t line = 0;
        if (memcpy_s(&line, sizeof(int32_t), pBuffer, i4) != EOK) {
            return {};
        }
        pBuffer += i4;
        
        int32_t column = 0;
        if (memcpy_s(&column, sizeof(int32_t), pBuffer, i4) != EOK) {
            return {};
        }
        pBuffer += i4;
        
        Position begin{fileID, line, column};

        uint16_t isSingle = 0;
        if (memcpy_s(&isSingle, sizeof(uint16_t), pBuffer, sizeof(uint16_t)) != EOK) {
            return {};
        }
        pBuffer += sizeof(uint16_t);
        
        unsigned delimiterNum{1};
        if (static_cast<TokenKind>(kind) == TokenKind::MULTILINE_RAW_STRING) {
            if (memcpy_s(&delimiterNum, sizeof(uint16_t), pBuffer, sizeof(uint16_t)) != EOK) {
                return {};
            }
            pBuffer += sizeof(uint16_t);
        }
        
        Position end{begin == INVALID_POSITION ? INVALID_POSITION
            : begin + GetTokenLength(value.size(), static_cast<TokenKind>(kind), delimiterNum)};
        Token token{static_cast<TokenKind>(kind), std::move(value), begin, end};
        token.delimiterNum = delimiterNum;
        token.isSingleQuote = (isSingle == 1);
        tokens.emplace_back(std::move(token));
    }
    return tokens;
}

uint8_t* TokenSerialization::GetTokensBytesWithHead(const std::vector<Token>& tokens)
{
    if (tokens.empty()) {
        return nullptr;
    }
    std::vector<uint8_t> tokensBytes = TokenSerialization::GetTokensBytes(tokens);
    size_t bufferSize = tokensBytes.size() + sizeof(uint32_t);
    if (bufferSize == 0 || bufferSize > std::numeric_limits<uint32_t>::max()) {
        Errorln("Memory Allocated Size is Not Valid.");
        return nullptr;
    }
    uint8_t* rawPtr = (uint8_t*)malloc(bufferSize);
    if (rawPtr == nullptr) {
        Errorln("Memory Allocation Failed.");
        return rawPtr;
    }
    uint32_t head = static_cast<uint32_t>(bufferSize);
    auto pHead = reinterpret_cast<uint8_t*>(&head);
    (void)tokensBytes.insert(tokensBytes.begin(), pHead, pHead + sizeof(uint32_t));
    (void)std::copy(tokensBytes.begin(), tokensBytes.end(), rawPtr);
    return rawPtr;
}