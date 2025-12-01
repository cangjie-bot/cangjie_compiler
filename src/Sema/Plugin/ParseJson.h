// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares json parsing functions.
 */

#ifndef PARSE_JSON_H
#define PARSE_JSON_H

#include <cstdint>
#include <string>
#include <vector>

#include "cangjie/Utils/SafePointer.h"

namespace Cangjie {
namespace PluginCheck {

struct JsonObject;

struct JsonPair {
    std::string key;
    std::vector<std::string> valueStr;
    std::vector<OwnedPtr<JsonObject>> valueObj;
    std::vector<uint64_t> valueNum;
};

struct JsonObject {
    std::vector<OwnedPtr<JsonPair>> pairs;
};

enum class StringMod {
    KEY,
    VALUE,
};

OwnedPtr<JsonObject> ParseJsonObject(size_t& pos, const std::vector<uint8_t>& in);
std::vector<std::string> GetJsonString(Ptr<JsonObject> root, const std::string& key);
Ptr<JsonObject> GetJsonObject(Ptr<JsonObject> root, const std::string& key, const size_t index);
} // namespace PluginCheck
} // namespace Cangjie
#endif