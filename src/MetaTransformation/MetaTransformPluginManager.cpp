// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "cangjie/MetaTransformation/MetaTransform.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Basic/Version.h"

namespace Cangjie {
template class MetaTransformPluginManager<MetaKind::CHIR>;

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
namespace {
class MetaTransformPlugin {
public:
    static std::optional<MetaTransformPlugin> Get(const std::string& path);
    void RegisterCallbackTo(MetaTransformPluginBuilder& mtm) const;

    bool IsValid() const
    {
        return !pluginPath.empty() && metaTransformPluginInfo.cjcVersion == CANGJIE_VERSION &&
            metaTransformPluginInfo.registerTo;
    }

    void* GetHandle() const
    {
        return handle;
    }

private:
    MetaTransformPlugin() = default;
    MetaTransformPlugin(const std::string& pluginPath, const MetaTransformPluginInfo& info, HANDLE handle);

private:
    std::string pluginPath;
    MetaTransformPluginInfo metaTransformPluginInfo;
    HANDLE handle;
};

MetaTransformPlugin::MetaTransformPlugin(
    const std::string& pluginPath, const MetaTransformPluginInfo& info, HANDLE handle)
    : pluginPath(pluginPath), metaTransformPluginInfo(info), handle(handle)
{
}

std::optional<MetaTransformPlugin> MetaTransformPlugin::Get(const std::string& path)
{
    HANDLE handle = nullptr;
#ifdef _WIN32
    handle = InvokeRuntime::OpenSymbolTable(path);
#elif defined(__linux__) || defined(__APPLE__)
    handle = InvokeRuntime::OpenSymbolTable(path, RTLD_NOW | RTLD_LOCAL);
#endif
    if (!handle) {
#ifndef CANGJIE_ENABLE_GCOV
        throw NullPointerException();
#else
        CJC_ABORT();
#endif
    }
    void* fPtr = InvokeRuntime::GetMethod(handle, "getMetaTransformPluginInfo");
    if (!fPtr) {
        return {};
    }
    auto pluginInfo = reinterpret_cast<MetaTransformPluginInfo (*)()>(fPtr)();
    return {MetaTransformPlugin(path, pluginInfo, handle)};
}

void MetaTransformPlugin::RegisterCallbackTo(MetaTransformPluginBuilder& mtm) const
{
    metaTransformPluginInfo.registerTo(mtm);
}
} // namespace

void* FindEntryInPlugin(const CompilerInvocation& invocation, const std::string& path);

bool CompilerInstance::PerformPluginLoad()
{
    for (auto pluginPath : invocation.globalOptions.pluginPaths) { // loop for all plugins
#ifndef CANGJIE_ENABLE_GCOV
        try {
#endif
            if (auto cppCHIRPlugin = MetaTransformPlugin::Get(pluginPath)) {
                auto& metaTransformPlugin = *cppCHIRPlugin;
                if (!metaTransformPlugin.IsValid()) {
                    diag.DiagnoseRefactor(DiagKindRefactor::not_a_valid_plugin, DEFAULT_POSITION, pluginPath);
                    continue;
                }
                AddPluginHandle(metaTransformPlugin.GetHandle());
                metaTransformPlugin.RegisterCallbackTo(metaTransformPluginBuilder); // register MetaTransform into builder
            } else if (auto sym = FindEntryInPlugin(invocation, pluginPath)) {
                auto registerPlugin = reinterpret_cast<void (*)()>(sym);
                registerPlugin();
                invocation.globalOptions.astPluginPaths.push_back(pluginPath);
            } else {
                diag.DiagnoseRefactor(DiagKindRefactor::not_a_valid_plugin, DEFAULT_POSITION, pluginPath);
                return false;
            }
#ifndef CANGJIE_ENABLE_GCOV
        } catch (...) {
            diag.DiagnoseRefactor(DiagKindRefactor::not_a_valid_plugin, DEFAULT_POSITION, pluginPath);
            return false;
        }
#endif
    }
    return diag.GetErrorCount() == 0;
}
#endif
}
#endif
