// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Job Class.
 */

#include "Job.h"

#include "cangjie/Basic/Print.h"
#include "cangjie/Driver/Tool.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "cangjie/Driver/Backend/CJNATIVEBackend.h"
#endif
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Driver/Toolchains/ToolChain.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/Utils/Semaphore.h"
#include <filesystem>
namespace fs = std::filesystem;
#include <fstream>

namespace {
bool CheckExecuteResult(std::map<std::string, std::unique_ptr<ToolFuture>>& checklist,
    bool returnIfAnyToolFinished = false)
{
    auto printError = [](std::string cmd) {
        if (!TempFileManager::Instance().IsDeleted()) {
            Errorln(cmd, ": command failed (use -V to see invocation)");
        }
    };
    bool success = true;
    while (!checklist.empty()) {
        size_t totalTasks = checklist.size();
        for (auto it = checklist.cbegin(); it != checklist.cend();) {
            auto state = it->second->GetState();
            if (state == ToolFuture::State::FAILED) {
                Utils::Semaphore::Get().Release();
                printError(it->first);
                checklist.erase(it++);
                success = false;
            } else if (state == ToolFuture::State::SUCCESS) {
                Utils::Semaphore::Get().Release();
                checklist.erase(it++);
            } else {
                ++it;
            }
        }
        if (returnIfAnyToolFinished && totalTasks != checklist.size()) {
            // Some tasks are finished and removed from the list.
            return success;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200)); // Check running tasks every 200 ms.
    }
    return success;
}
} // namespace

using namespace Cangjie;

bool Job::Assemble(const DriverOptions& driverOptions, const Driver& driver)
{
    switch (driverOptions.backend) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case Triple::BackendType::CJNATIVE:
            backend = std::make_unique<CJNATIVEBackend>(*this, driverOptions, driver);
            break;
#endif
        case Triple::BackendType::UNKNOWN:
        default:
            Errorln("Toolchain: Unsupported backend");
            return false;
    }

    if (!backend->Generate()) {
        return false;
    }

    verbose = driverOptions.enableVerbose;

    return true;
}

bool Job::Execute(bool dryLink) const
{
    const std::vector<ToolBatch>& commandList = backend->GetBackendCmds();
    auto needSkipLink = false;
    auto& lastBatch = commandList.back();
    std::string linkCmd = "";
    if (dryLink && lastBatch.size() == 1 && FileUtil::GetFileName(lastBatch[0]->GetName()) == "ld64.lld") {
        Println("shit needSkipLink = true");
        needSkipLink = true;
        linkCmd = FileUtil::Normalize(lastBatch[0]->GetCommandString());
    }
    else {
        Println("shit needSkipLink = false "+FileUtil::GetFileName(lastBatch[0]->GetName()));
    }
    std::string srcPath = "";
    std::string dstPath = "";
    auto size = commandList.size();
    size_t cnt = 0;
    for (const ToolBatch& cmdBatch : commandList) {
        cnt++;
        if (cnt == size && needSkipLink)
        {
            continue;
        }
        if (cmdBatch.empty()) {
            continue;
        }
        std::map<std::string, std::unique_ptr<ToolFuture>> childWorkers{};
        Utils::ProfileRecorder recorder("Main Stage", "Execute " + FileUtil::GetFileName(cmdBatch[0]->GetName()), "");
        Println("-----------------------------------------------------------------------------------" + FileUtil::GetFileName(cmdBatch[0]->GetName()));
        for (auto& cmd : cmdBatch) {
            Println("xxx" + FileUtil::Normalize(cmd->GetCommandString()));
        }
        Println("-----------------------------------------------------------------------------------" + FileUtil::GetFileName(cmdBatch[0]->GetName()));
        if (FileUtil::GetFileName(cmdBatch[0]->GetName()) == "CacheCopy") {
            auto args = cmdBatch[0]->GetArgs();
            srcPath = args[0];
            dstPath = args[1];
            for (auto& arg : args) {
                Println("CacheCopy arg " + arg);
            }
        }
        for (auto& cmd : cmdBatch) {
            // NOTE: `CheckExecuteResult` acquires semaphore without condition. We must ensure that there is still
            // available slot in semaphore before executing next command. If there is no more slot available, wait
            // any of previous created threads finish.
            while (Utils::Semaphore::Get().GetCount() == 0) {
                if (!CheckExecuteResult(childWorkers, true)) {
                    return false;
                }
            }
            auto future = cmd->Execute(verbose);
            if (!future) {
                return false;
            }
            childWorkers.emplace(cmd->GetCommandString(), std::move(future));
        }
        if (!CheckExecuteResult(childWorkers)) {
            return false;
        }
    }
    if (needSkipLink) {
        fs::path src_file_path = srcPath;
        fs::path dst_file_path = dstPath;
        fs::path grandparent = dst_file_path.parent_path().parent_path();
        fs::path filename = src_file_path.filename();
        fs::path full_path = grandparent / filename;
        fs::copy(src_file_path, full_path);
        fs::path link_path = (grandparent / src_file_path.stem()).string() + ".link";
        size_t pos = linkCmd.find(src_file_path);
        if (pos != std::string::npos) {
            linkCmd.replace(pos, src_file_path.string().length(), full_path);
        }
        std::ofstream out_file(link_path);
        out_file << linkCmd;
        out_file.close();
    }
    return true;
}