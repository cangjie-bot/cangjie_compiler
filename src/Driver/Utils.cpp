// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the utility classes and functions.
 */

#include "cangjie/Driver/Utils.h"

#include <sstream>

#include "cangjie/Basic/Print.h"
#include "cangjie/Utils/FileUtil.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

namespace Cangjie {
std::string GetSingleQuoted(const std::string& str)
{
    std::stringstream ss;
    ss << "'";
    for (char c : str) {
        // backslash cannot be used as escape character in Shell Command Language. To be able to
        // use single quote in a command, we generate two single quoted string and join them with
        // `\'`. For example, ab'cd is transformed to 'ab'\''cd'.
        if (c == '\'') {
            ss << "'\\''";
        } else {
            ss << c;
        }
    }
    ss << "'";
    return ss.str();
}

std::string GetCommandLineArgumentQuoted(const std::string& arg)
{
#ifdef _WIN32
    return FileUtil::GetQuoted(arg);
#else
    return GetSingleQuoted(arg);
#endif
}

std::vector<std::string> PrependToPaths(const std::string& prefix, const std::vector<std::string>& paths, bool quoted)
{
    std::vector<std::string> searchPaths;
    for (const auto& path : paths) {
        std::string tmp = quoted ? FileUtil::GetQuoted(prefix + path) : (prefix + path);
        searchPaths.emplace_back(tmp);
    }
    return searchPaths;
}

std::optional<std::string> GetDarwinSDKVersion(const std::string& sdkPath)
{
    auto settingFilePath = FileUtil::JoinPath(sdkPath, "SDKSettings.json");
    std::string errMessage;
    std::optional<std::string> maybeSettingFileContent = FileUtil::ReadFileContent(settingFilePath, errMessage);
    if (!maybeSettingFileContent.has_value()) {
        return std::nullopt;
    }
    llvm::Expected<llvm::json::Value> result = llvm::json::parse(maybeSettingFileContent.value());
    if (!result) {
        return std::nullopt;
    }
    if (const llvm::json::Object *obj = result->getAsObject()) {
        auto value = obj->getString("Version");
        if (!value) {
            return std::nullopt;
        }
        return value->str();
    }
    return std::nullopt;
}

std::vector<std::string> readLinkSectionFromObjectFile(const std::string& objFile) {
    auto File = llvm::MemoryBuffer::getFile(objFile);
    std::vector<std::string> content{};
    if(auto error = File.getError()) {
        Errorln("Can't open file: " + error.message());
        return content;
    }
    auto memBufferRef = File->get()->getMemBufferRef();
    auto binFile = llvm::object::createBinary(memBufferRef);
    if(!binFile) {
        Errorln(objFile + " isn't not a recognized object file.");
        return content;
    }
    std::unique_ptr<llvm::object::Binary> Bin(std::move(binFile.get()));
    if(auto* objContent = llvm::dyn_cast<llvm::object::ObjectFile>(Bin.get())) {
        for(auto& sec : objContent->sections()) {
            auto nameRef = sec.getName();
            if(!nameRef) {
                continue;
            }
            std::string name = nameRef->str();
            if(name == ".linker-options") {
                auto contentRef = sec.getContents();
                if(!contentRef) {
                    continue;
                }
                std::string contentStr = contentRef->str();
                content = Utils::SplitString(contentStr, "@");
                break;
            }
        }
    }
    return content;
}

bool endsWith(const std::string& a, const std::string& b) {
    if(a.size() < b.size())
    {
        return false;
    }
    return a.rfind(b) + b.size() == a.size();
}

} // namespace Cangjie
