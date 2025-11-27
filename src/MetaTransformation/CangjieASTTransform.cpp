// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.
\
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Macro/InvokeUtil.h"
#include "cangjie/Driver/DriverOptions.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/Utils/Utils.h"
#include <flatbuffers/flatbuffers.h>

namespace Cangjie {
using namespace AST;

void* FindSymbolInStdLibs(const GlobalOptions& opts, std::string_view libName, std::string_view funName, bool load)
{
    // find libstdx.syntaxFFI.so from LIBRARY_PATH or -L options
    std::vector<std::string> searchPaths(opts.environment.libraryPaths);
    searchPaths.insert(searchPaths.end(), opts.librarySearchPaths.begin(), opts.librarySearchPaths.end());
    std::string libExt = GlobalOptions::GetSharedLibraryExtension(opts.target.os);    
    auto libPath = FileUtil::FindFileByName(std::string(libName) + libExt, searchPaths);
    if (!libPath.has_value()) {
        Errorln("Could not find library: ", libName);
        return nullptr;
    }
    HANDLE handle = InvokeRuntime::OpenSymbolTable(libPath.value());
    if (!handle) {
        Errorln("Could not load library: ", libPath.value());
        return nullptr;
    }
    if (load) {
        if (int ret = RuntimeInit::GetInstance().initLibFunc(libPath->c_str()); ret != 0) {
            Errorln("Could not initialize library: ", libPath.value());
            return nullptr;
        }
    }
    void* funcPtr = InvokeRuntime::GetMethod(handle, funName.data());
    if (!funcPtr) {
        Errorln("Could not find function: ", funName);
        return nullptr;
    }
    return funcPtr;
}

bool RegisterASTPlugin(const std::string& astPluginPath)
{
    void* lib = dlopen(astPluginPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        Errorln("Could not open library: ", astPluginPath);
        return false;
    }
    if (int ret = RuntimeInit::GetInstance().initLibFunc(astPluginPath.c_str()); ret != 0) {
        Errorln("Could not initialize library: ", astPluginPath);
        return false;
    }
    auto registerPlugin = reinterpret_cast<void (*)()>(InvokeRuntime::GetMethod(lib, "registerPlugin"));
    if (!registerPlugin) {
        Errorln("Invalid plugin: could not find function `registerPlugin` in ", astPluginPath);
        return false;
    }
    registerPlugin();
    return true;
}

namespace {
void WriteInt32(std::vector<char>& buf, int32_t v)
{
    buf.insert(buf.end(), reinterpret_cast<char*>(&v), reinterpret_cast<char*>(&v) + 4);
}

void WriteString(std::vector<char>& buf, const std::string& s)
{
    WriteInt32(buf, static_cast<int32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

void WriteStringVector(std::vector<char>& buf, const std::vector<std::string>& v)
{
    WriteInt32(buf, static_cast<int32_t>(v.size()));
    for (const auto& s : v) {
        WriteString(buf, s);
    }
}

void WriteStringMap(std::vector<char>& buf, const std::unordered_map<std::string, std::string>& m)
{
    WriteInt32(buf, static_cast<int32_t>(m.size()));
    for (const auto& [k, v] : m) {
        WriteString(buf, k);
        WriteString(buf, v);
    }
}

// to be called after class Option in stdx.plugin is final
[[maybe_unused]] const char* SerialiseOptions(const GlobalOptions& opts)
{
    std::vector<char> buf;
    buf.reserve(4096);
    buf.push_back(static_cast<char>(opts.target.arch));
    buf.push_back(static_cast<char>(opts.target.vendor));
    buf.push_back(static_cast<char>(opts.target.os));
    buf.push_back(static_cast<char>(opts.target.env));
    uint8_t flags = 0;
    flags |= opts.enableCompileTest ? 0x01 : 0;
    flags |= opts.enableCompileDebug ? 0x02 : 0;
    flags |= opts.strictNumberMode ? 0x04 : 0;
    flags |= opts.disableReflection ? 0x08 : 0;
    flags |= opts.enableCoverage ? 0x10 : 0;
    flags |= opts.experimentalMode ? 0x20 : 0;
    buf.push_back(static_cast<char>(flags));
    buf.push_back(static_cast<char>(opts.optimizationLevel));
    buf.push_back(static_cast<char>(opts.outputMode));
    buf.push_back(static_cast<char>(opts.mock));
    buf.push_back(static_cast<char>(opts.sanitizerType));
    WriteString(buf, opts.moduleName);
    WriteString(buf, opts.moduleSrcPath);
    WriteString(buf, opts.cangjieHome);
    WriteString(buf, opts.output);
    WriteStringVector(buf, opts.importPaths);
    WriteStringVector(buf, opts.librarySearchPaths);
    WriteStringVector(buf, opts.srcFiles);
    WriteStringMap(buf, opts.passedWhenKeyValue);
    size_t totalSize = sizeof(int32_t) + buf.size();
    char* result = new char[totalSize];
    int32_t size = static_cast<int32_t>(buf.size());
    std::memcpy(result, &size, sizeof(size));
    std::memcpy(result + sizeof(size), buf.data(), buf.size());
    return result;
}
} // anonymous namespace

bool ExecuteASTPlugins(const AST::Package& package, CompilerInstance& ci, CompileStage stage)
{
    auto executeASTPlugins = reinterpret_cast<bool (*)(const void*, int32_t)>(FindSymbolInStdLibs(
        ci.invocation.globalOptions, "libstdx.plugin", "executeASTPlugins", true));
    if (!executeASTPlugins) {
        Errorln("Could not find executeASTPlugins function");
        return false;
    }
    // must pass integer, because cangjie enum is not expressible in C++
    auto res = executeASTPlugins(&package, stage == CompileStage::MACRO_EXPAND ? 0 : 1);
    return res;
}

namespace {
int32_t ReadInt32(const uint8_t*& ptr)
{
    int32_t value;
    std::memcpy(&value, ptr, sizeof(value));
    ptr += sizeof(value);
    return value;
}

std::string ReadString(const uint8_t*& ptr)
{
    int32_t len = ReadInt32(ptr);
    std::string result(reinterpret_cast<const char*>(ptr), static_cast<size_t>(len));
    ptr += len;
    return result;
}

struct PluginDiagnostic {
    DiagKindRefactor severity;
    Range range;
    std::string kind;
    std::string msg;
    std::string filePath;

    bool operator<(const PluginDiagnostic& other) const
    {
        return range.begin < other.range.begin;
    }
};

std::vector<PluginDiagnostic> GetDiagnostics(CompilerInstance& ci)
{
    std::vector<PluginDiagnostic> result;
    auto getDiagnostics = reinterpret_cast<void* (*)()>(FindSymbolInStdLibs(
        ci.invocation.globalOptions, "libstdx.plugin", "getDiagnostics", true));
    if (!getDiagnostics) {
        Errorln("Could not find getDiagnostics function");
        return result;
    }
    auto buffer = static_cast<uint8_t*>(getDiagnostics());
    if (!buffer) {
        return result;
    }

    const uint8_t* ptr = buffer;
    int32_t count = ReadInt32(ptr);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t type = *ptr++;
        // Read type: 0 = Error, 1 = Warning
        auto severity = (type == 0) ? DiagKindRefactor::sema_plugin_error : DiagKindRefactor::sema_plugin_warning;
        std::string kind = ReadString(ptr);
        std::string msg = ReadString(ptr);
        std::string filePath = ReadString(ptr);
        // Try to get fileID from SourceManager, use -1 as marker if not found
        auto fileID = ci.sm.GetFileID(filePath);
        int32_t beginLine = ReadInt32(ptr);
        int32_t beginColumn = ReadInt32(ptr);
        int32_t endLine = ReadInt32(ptr);
        int32_t endColumn = ReadInt32(ptr);
        Position beginPos(static_cast<unsigned>(fileID), beginLine, beginColumn);
        Position endPos(static_cast<unsigned>(fileID), endLine, endColumn);
        Range range = MakeRange(beginPos, endPos);
        result.push_back({severity, range, std::move(kind), std::move(msg), filePath});
    }
    free(buffer);
    return result;
}

void ReportDiagnostics(DiagnosticEngine& diag, std::vector<PluginDiagnostic>&& diagnostics)
{
    std::sort(diagnostics.begin(), diagnostics.end());
    for (const auto& d : diagnostics) {
        auto builder = diag.DiagnoseRefactor(d.severity, d.range, d.kind, d.msg);
        builder.diagnostic.pluginFilePath = d.filePath;
    }
}
} // anonymous namespace

void* FindEntryInPlugin(const CompilerInvocation& invocation, const std::string& path)
{
    HANDLE handle = InvokeRuntime::OpenSymbolTable(path);
    if (!handle) {
        return {};
    }
    RuntimeInit::GetInstance().InitRuntime(invocation.GetRuntimeLibPath());
    if (int ret = RuntimeInit::GetInstance().initLibFunc(path.c_str()); ret != 0) {
        return {};
    }
    void* fPtr = InvokeRuntime::GetMethod(handle, "registerPlugin");
    return fPtr;
}

bool CompilerInstance::ExecuteASTPlugins(CompileStage stage)
{
    if (invocation.globalOptions.astPluginPaths.empty()) {
        return true;
    }
    Utils::ProfileRecorder profileRecorder("ExecuteASTPlugins", "ExecuteASTPlugins");
    auto ret = Cangjie::ExecuteASTPlugins(*GetSourcePackages()[0], *this, stage);
    ReportDiagnostics(diag, GetDiagnostics(*this));
    return ret && diag.GetErrorCount() == 0;
}
} // namespace Cangjie