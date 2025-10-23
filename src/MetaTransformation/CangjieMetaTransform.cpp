// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/MetaTransformation/CangjieMetaTransform.h"
#include "cangjie/CHIR/CHIR.h"
#include "cangjie/CHIR/Serializer/CHIRSerializer.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"
#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif

namespace Cangjie {
namespace {
using namespace CHIR;
#if !defined(__linux__) && !defined(__APPLE__)
constexpr size_t ERR_UNSUPPORTED_PLATFORM = 3; // non-Linux/macOS
#else
// Error codes returned via Memory.size when Memory.ptr is nullptr
constexpr size_t ERR_CHILD_FAILURE = 1;       // child/plugin failure
constexpr size_t ERR_RESOURCE_FAILURE = 2;    // OOM or system call failure
#endif

class MyDeserialize {
public:
    MyDeserialize(CHIRBuilder& builder, const Memory& m) : b(builder), s(static_cast<const char*>(m.ptr), m.size) {}

    Package* Deserialize()
    {
        Skip("package: ");
        auto end = s.find('\n', i);
        std::string_view line = (end == std::string_view::npos) ? s.substr(i) : s.substr(i, end - i);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        i = (end == std::string_view::npos) ? s.size() : end + 1;
        auto pkg = b.CreatePackage(std::string(line));
        Skip("global vars:\n");
        auto parsedGVs = ParseGvsUntilBlank();
        for (auto& v: parsedGVs) {
            auto gv = b.CreateGlobalVar(INVALID_LOCATION, StaticCast<RefType>(ToCHIRType(v)), v.identifier.substr(1), v.identifier,
                v.identifier, pkg->GetName());
            auto init = b.CreateLiteralValue<IntLiteral>(GetInitializerType(v), v.value);
            gv->SetInitializer(*init);
        }
        return pkg;
    }

    /// Skip the given constant string in the input.
    void Skip(std::string_view constantStr)
    {
        i += constantStr.size();
    }

    CHIRBuilder& b;
    std::string_view s;
    std::size_t i{0};

    // ===== Handwritten parser =====
    // Grammar (simplified):
    // gvs: gv ('\n' gv)*;
    // gv: '  ' identifier ': ' type ' = ' constant;
    // identifier: '@' [a-zA-Z_@$] [a-zA-Z_@$0-9]+;
    // type: ('Int64' | 'UInt8') '&'?;
    // constant: 'Constant(' integer ')';
    // integer: [0-9]+ integerSuffix; integerSuffix: ('i'|'u') ('8'|'64');

    struct GV {
        std::string identifier;
        std::string typeName; // Int64 | UInt8
        bool isRef{false};    // '&'
        unsigned baseBits{0}; // 8 or 64 from suffix
        bool isUnsigned{false};
        unsigned long long value{0};
    };

    Type* ToCHIRType(const GV& gv)
    {
        Type* base{};
        if (gv.typeName == "Int64") {
            base = b.GetInt64Ty();
        } else if (gv.typeName == "UInt8") {
            base = b.GetUInt8Ty();
        } else if (gv.typeName == "Bool") {
            base = b.GetBoolTy();
        } else {
            CJC_ASSERT(false && "unknown type");
        }
        if (gv.isRef) {
            return b.GetType<RefType>(base);
        }
        return base;
    }

    Type* GetInitializerType(const GV& gv)
    {
        switch (gv.baseBits) {
            case 8:
                return gv.isUnsigned ? b.GetUInt8Ty() : b.GetInt8Ty();
            case 64:
                return gv.isUnsigned ? b.GetUInt64Ty() : b.GetInt64Ty();
            default:
                CJC_ASSERT(false && "unsupported base bits");
                return nullptr;
        }
    }

    std::vector<GV> ParseGvsUntilBlank()
    {
        std::vector<GV> out;
        size_t save = i;
        GV one{};
        if (ParseGv(one)) {
            out.push_back(std::move(one));
            while (AtNL(i)) {
                ConsumeNL(i);
                if (AtNL(i)) {
                    // blank line terminator
                    break;
                }
                GV next{};
                if (ParseGv(next)) {
                    out.push_back(std::move(next));
                } else {
                    // no more gvs; stop and rewind to line start before failure
                    break;
                }
            }
        } else {
            i = save;
        }
        return out;
    }

    bool ParseGv(GV& out)
    {
        size_t j = i;
        // '  ' (two spaces)
        if (!(j + 2 <= s.size() && s[j] == ' ' && s[j + 1] == ' ')) {
            return false;
        }
        j += 2;

        // identifier
        std::string ident;
        if (!ParseIdentifier(j, ident)) {
            return false;
        }

        // ': '
        if (!(j + 2 <= s.size() && s[j] == ':' && s[j + 1] == ' ')) {
            return false;
        }
        j += 2;

        // type
        std::string typeName;
        bool isRef = false;
        if (!ParseType(j, typeName, isRef)) {
            return false;
        }

        // ' = '
        if (!(j + 3 <= s.size() && s[j] == ' ' && s[j + 1] == '=' && s[j + 2] == ' ')) {
            return false;
        }
        j += 3;

        // constant
        unsigned long long val = 0;
        bool isUnsigned = false;
        unsigned bits = 0;
        if (!ParseConstant(j, val, isUnsigned, bits)) {
            return false;
        }

        // end of line
        if (!AtNL(j)) {
            return false;
        }

        // commit
        out.identifier = std::move(ident);
        out.typeName = std::move(typeName);
        out.isRef = isRef;
        out.value = val;
        out.isUnsigned = isUnsigned;
        out.baseBits = bits;
        i = j;
        return true;
    }

    bool ParseIdentifier(size_t& j, std::string& out)
    {
        size_t start = j;
        if (j >= s.size() || s[j] != '@') {
            return false;
        }
        ++j;
        auto isHead = [](unsigned char c) {
            return std::isalpha(c) || c == '_' || c == '@' || c == '$';
        };
        auto isTail = [&](unsigned char c) {
            return isHead(c) || std::isdigit(c);
        };
        if (j >= s.size() || !isHead(static_cast<unsigned char>(s[j]))) {
            return false;
        }
        ++j;
        if (j >= s.size() || !isTail(static_cast<unsigned char>(s[j]))) {
            return false; // requires at least one tail char
        }
        while (j < s.size() && isTail(static_cast<unsigned char>(s[j]))) {
            ++j;
        }
        out.assign(s.substr(start, j - start));
        return true;
    }

    bool ParseType(size_t& j, std::string& typeName, bool& isRef)
    {
        if (Starts(j, "Int64")) {
            typeName = "Int64";
            j += 5;
        } else if (Starts(j, "UInt8")) {
            typeName = "UInt8";
            j += 5;
        } else if (Starts(j, "Bool")) {
            typeName = "Bool";
            j += 4;
        } else {
            return false;
        }
        if (j < s.size() && s[j] == '&') {
            isRef = true;
            ++j;
        } else {
            isRef = false;
        }
        return true;
    }

    bool ParseConstant(size_t& j, unsigned long long& val, bool& isUnsigned, unsigned& bits)
    {
        if (!Starts(j, "Constant(")) {
            return false;
        }
        j += 9; // strlen("Constant(")
        if (!ParseInteger(j, val, isUnsigned, bits)) {
            return false;
        }
        if (j >= s.size() || s[j] != ')') {
            return false;
        }
        ++j;
        return true;
    }

    bool ParseInteger(size_t& j, unsigned long long& val, bool& isUnsigned, unsigned& bits)
    {
        if (j >= s.size() || !std::isdigit(static_cast<unsigned char>(s[j]))) {
            return false;
        }
        unsigned long long v = 0;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
            v = v * 10ull + static_cast<unsigned long long>(s[j] - '0');
            ++j;
        }
        bool uflag = false;
        unsigned bitsTmp = 0;
        if (!ParseIntegerSuffix(j, uflag, bitsTmp)) {
            return false;
        }
        val = v;
        isUnsigned = uflag;
        bits = bitsTmp;
        return true;
    }

    bool ParseIntegerSuffix(size_t& j, bool& isUnsigned, unsigned& bits)
    {
        if (j >= s.size()) {
            return false;
        }
        char iu = s[j];
        if (iu != 'i' && iu != 'u') {
            return false;
        }
        ++j;
        if (Starts(j, "64")) {
            bits = 64;
            j += 2;
        } else if (j < s.size() && s[j] == '8') {
            bits = 8;
            ++j;
        } else {
            return false;
        }
        isUnsigned = (iu == 'u');
        return true;
    }

private:
    inline bool Starts(size_t pos, std::string_view lit) const
    {
        if (pos + lit.size() > s.size()) {
            return false;
        }
        return s.compare(pos, lit.size(), lit) == 0;
    }
    inline bool AtNL(size_t pos) const
    {
        if (pos >= s.size()) {
            return false;
        }
        if (s[pos] == '\n') {
            return true;
        }
        if (s[pos] == '\r' && pos + 1 < s.size() && s[pos + 1] == '\n') {
            return true;
        }
        return false;
    }
    inline void ConsumeNL(size_t& pos) const
    {
        if (pos < s.size() && s[pos] == '\r') {
            ++pos;
        }
        if (pos < s.size() && s[pos] == '\n') {
            ++pos;
        }
    }
};

Memory RunPlugin(Memory pkg, std::string_view pluginPath, CompilerInvocation& invoc)
{
    // Linux-only implementation using a control pipe + POSIX shared memory (shm_open)
#if !defined(__linux__) && !defined(__APPLE__)
    (void)pkg;
    (void)pluginPath;
    (void)invoc;
    Errorln("RunPlugin is only implemented on Linux/macOS at the moment");
    return Memory{nullptr, ERR_UNSUPPORTED_PLATFORM};
#else
    // Support MAP_ANONYMOUS on Linux and MAP_ANON on macOS
    #ifndef MAP_ANONYMOUS
    #define MAP_ANONYMOUS MAP_ANON
    #endif
    // Prepare a small shared input region for the child to read configuration and input
    auto runtimePath = invoc.GetRuntimeLibPath();
    auto& stackVar = invoc.globalOptions.environment.allVariables[G_CJSTACKSIZE];
    auto& heapVar = invoc.globalOptions.environment.allVariables[G_CJHEAPSIZE];
    size_t totalSize = sizeof(size_t) * 5 + pluginPath.size() + 1 + runtimePath.size() + 1 +
        stackVar.size() + 1 + heapVar.size() + 1 + pkg.size;
    void* mem = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        Errorln("Failed to allocate shared memory for meta transform plugin inputs");
        return Memory{nullptr, ERR_RESOURCE_FAILURE};
    }
    // Layout: [len,str0][len,str1][len,str2][len,str3][pkg.size][pkg.bytes]
    auto ptr = static_cast<unsigned char*>(mem);
    auto writeString = [&ptr](std::string_view str) {
        size_t len = str.size();
        std::memcpy(ptr, &len, sizeof(size_t));
        ptr += sizeof(size_t);
        std::memcpy(ptr, str.data(), len + 1); // include null terminator
        ptr += len + 1;
    };
    writeString(pluginPath);
    writeString(runtimePath);
    writeString(stackVar);
    writeString(heapVar);
    std::memcpy(ptr, &pkg.size, sizeof(size_t));
    ptr += sizeof(size_t);
    std::memcpy(ptr, pkg.ptr, pkg.size);

    // Create a POSIX shared memory object (resized by child) and a control pipe to receive the size
    int ctrlPipe[2];
    if (pipe(ctrlPipe) != 0) {
        Errorln("Failed to create control pipe for meta transform");
        munmap(mem, totalSize);
        return Memory{nullptr, ERR_RESOURCE_FAILURE};
    }
    // Unique shm name based on pid and time
    char shmName[64];
    snprintf(shmName, sizeof(shmName), "/cj_meta_out_%d_%llx", static_cast<int>(getpid()),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mem)));
    int shmFd = shm_open(shmName, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shmFd < 0) {
        Errorln("Failed to shm_open for meta transform output");
        close(ctrlPipe[0]);
        close(ctrlPipe[1]);
        munmap(mem, totalSize);
        return Memory{nullptr, ERR_RESOURCE_FAILURE};
    }

    pid_t pid = fork();
    if (pid < 0) {
        Errorln("Failed to fork process for meta transform plugin");
        close(shmFd);
        shm_unlink(shmName);
        close(ctrlPipe[0]);
        close(ctrlPipe[1]);
        munmap(mem, totalSize);
        return Memory{nullptr, ERR_RESOURCE_FAILURE};
    }
    if (pid == 0) {
        // Child: read inputs from shared region, run plugin, write variable-sized output to shmFd
        // Close read end in child for clarity
        close(ctrlPipe[0]);

        // Lambda to safely write a size_t to the control pipe with EINTR handling
        auto writeSize = [&](size_t value) {
            ssize_t wrote = -1;
            do {
                wrote = ::write(ctrlPipe[1], &value, sizeof(value));
            } while (wrote == -1 && errno == EINTR);
            if (wrote != static_cast<ssize_t>(sizeof(value))) {
                // Best-effort log; avoid complex I/O in child
                // Fall through – parent will detect via pipe read failure
                return false;
            }
            return true;
        };

        auto pt = static_cast<unsigned char*>(mem);
        auto readString = [&pt]() {
            size_t len = 0;
            std::memcpy(&len, pt, sizeof(size_t));
            pt += sizeof(size_t);
            std::string_view str(reinterpret_cast<const char*>(pt), len);
            pt += len + 1; // include null terminator
            return str;
        };
        auto pluginPathSV = readString();
        auto runtimePathSV = readString();
        auto stackVarSV = readString();
        auto heapVarSV = readString();
        size_t pkgSize = 0;
        std::memcpy(&pkgSize, pt, sizeof(size_t));
        pt += sizeof(size_t);
        auto& runtime = RuntimeInit::GetInstance();
        std::unordered_map<std::string, std::string> envMap;
        if (!stackVarSV.empty()) {
            envMap[std::string{G_CJSTACKSIZE}] = std::string{stackVarSV};
        }
        if (!heapVarSV.empty()) {
            envMap[std::string{G_CJHEAPSIZE}] = std::string{heapVarSV};
        }
        bool runtimeInitSuccess = runtime.InitRuntime(std::string{runtimePathSV}, envMap);
        if (!runtimeInitSuccess) {
            Errorln("Failed to initialize Cangjie runtime in child process");
            (void)writeSize(0);
            _exit(ERR_RESOURCE_FAILURE);
        }
        auto invokeC = reinterpret_cast<void* (*)(void(*)(Memory*), Memory*)>(runtime.runtimeMethodFunc);
        auto initPlugin = reinterpret_cast<void (*)(const char*)>(runtime.initLibFunc);
        initPlugin(pluginPathSV.data());
        auto pluginHandle = InvokeRuntime::OpenSymbolTable(std::string{pluginPathSV}, RTLD_LAZY);
        if (!pluginHandle) {
            Errorln("Failed to open CHIR plugin dynamic library: ", pluginPathSV);
            // best effort to signal failure with size 0
            (void)writeSize(0);
            _exit(ERR_CHILD_FAILURE);
        }
        auto transformCHIRPackage = InvokeRuntime::GetMethod(pluginHandle, "transformCHIRPackage");
        if (!transformCHIRPackage) {
            Errorln("Failed to find transformCHIRPackage symbol in plugin: ", pluginPathSV);
            // best effort to signal failure with size 0
            (void)writeSize(0);
            _exit(ERR_CHILD_FAILURE);
        }
        auto fn = reinterpret_cast<void (*)(Memory*)>(transformCHIRPackage);
        Memory pkgMem{pt, pkgSize};
        auto cjThreadHandle = invokeC(fn, &pkgMem);
        auto getRet = reinterpret_cast<int (*)(const void*, void**)>(runtime.getRet);
        void* retPtr{};
        int retCode = getRet(cjThreadHandle, &retPtr);
        if (retCode != 0) {
            Errorln("CHIR plugin execution failed with code ", retCode);
            (void)writeSize(0);
            _exit(ERR_CHILD_FAILURE);
        }

        auto outPkg = pkgMem.ptr;
        auto size = pkgMem.size;
        if (size == 0 || outPkg == nullptr) {
            (void)writeSize(0);
            _exit(0);
        }

        // Resize shm to the exact size and copy the bytes
        if (ftruncate(shmFd, static_cast<off_t>(size)) != 0) {
            Errorln("Child failed to ftruncate shm for meta transform");
            (void)writeSize(0);
            _exit(ERR_RESOURCE_FAILURE);
        }
        void* outMap = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
        if (outMap == MAP_FAILED) {
            Errorln("Child failed to mmap shm for meta transform");
            (void)writeSize(0);
            _exit(ERR_RESOURCE_FAILURE);
        }
        std::memcpy(outMap, outPkg, size);
        // notify parent of size
        (void)writeSize(size);

        munmap(outMap, size);
        close(shmFd);
        // Child done
        _exit(0);
    }

    // Parent process
    // Close write end, wait for size
    close(ctrlPipe[1]);
    size_t outSize = 0;
    ssize_t nread = read(ctrlPipe[0], &outSize, sizeof(size_t));
    close(ctrlPipe[0]);

    int status = 0;
    (void)waitpid(pid, &status, 0);

    if (nread != static_cast<ssize_t>(sizeof(size_t)) || outSize == 0) {
        // Cleanup and return null on failure; map child's exit status to error codes
        size_t ec = ERR_CHILD_FAILURE; // default to child failure
        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            if (code == static_cast<int>(ERR_RESOURCE_FAILURE)) {
                ec = ERR_RESOURCE_FAILURE; // resource failure in child
            } else if (code == 0) {
                ec = ERR_CHILD_FAILURE; // zero-size output but child exited 0 -> treat as failure
            } else {
                ec = ERR_CHILD_FAILURE; // any non-zero -> child failure
            }
        }
        close(shmFd);
        shm_unlink(shmName);
        munmap(mem, totalSize);
        return Memory{nullptr, ec};
    }

    void* inMap = mmap(nullptr, outSize, PROT_READ, MAP_SHARED, shmFd, 0);
    if (inMap == MAP_FAILED) {
        Errorln("Parent failed to mmap shm for meta transform output");
        close(shmFd);
        shm_unlink(shmName);
        munmap(mem, totalSize);
        return Memory{nullptr, ERR_RESOURCE_FAILURE};
    }

    // Copy from shared memory into a malloc'ed buffer so the caller owns/frees it.
    void* outBuf = std::malloc(outSize);
    if (!outBuf) {
        munmap(inMap, outSize);
        close(shmFd);
        shm_unlink(shmName);
        munmap(mem, totalSize);
        return Memory{nullptr, ERR_RESOURCE_FAILURE};
    }
    std::memcpy(outBuf, inMap, outSize);
    munmap(inMap, outSize);
    close(shmFd);
    shm_unlink(shmName);
    munmap(mem, totalSize);
    return Memory{outBuf, outSize};
#endif
}

Package* Run(CHIRContext& ctx, const Package& p, std::string_view pluginPath, CompilerInvocation& invoc)
{
    auto fb = CHIRSerializer::Serialize(p, ToCHIR::Phase::RAW);
    Memory cppInput{fb.Data(), fb.Size()};
    auto cangjieOutput = RunPlugin(cppInput, pluginPath, invoc);
    if (!cangjieOutput.ptr) {
        // size carries error code: 1 child/plugin failure, 2 resource/OOM, 3 non-Linux/macOS
        return nullptr;
    }
    CHIRBuilder b{ctx};
    MyDeserialize myd(b, cangjieOutput);
    auto ret = myd.Deserialize();
    // cangjieOutput.ptr is malloc'ed in parent; free after use
    free(cangjieOutput.ptr);
    return ret;
}

void MergePackage(Package& old, Package& n)
{
    if (&old == &n) {
        return;
    }
    std::unordered_map<std::string_view, std::pair<GlobalVar*, size_t>> oldGvMap;
    auto oldGvs = old.GetGlobalVars();
    for (size_t i = 0; i < oldGvs.size(); ++i) {
        oldGvMap[oldGvs[i]->GetIdentifier()] = {oldGvs[i], i};
    }
    for (auto& gv : n.GetGlobalVars()) {
        if (auto it = oldGvMap.find(gv->GetIdentifier()); it == oldGvMap.end()) {
           oldGvs.push_back(gv);
        } else {
            oldGvs[it->second.second] = gv;
        }
    }
    old.SetGlobalVars(std::move(oldGvs));
    n.SetGlobalVars({});
}
}

bool CompilerInstance::ExecuteCHIRPlugins()
{
    if (cangjieCHIRPlugins.empty()) {
        return true;
    }
    // Do NOT initialize runtime in the parent when using forked plugin execution to avoid
    // deadlocks after fork in a multi-threaded process. Child will initialize runtime itself.
    auto pkg = chirData.GetCurrentCHIRPackage();
    for (auto &plugin : cangjieCHIRPlugins) {
        auto newPkg = Run(chirData.GetCHIRContext(), *pkg, plugin.first, invocation);
        if (!newPkg) {
            Errorln("run CHIR plugin returned null package");
            return false;
        }
        MergePackage(*pkg, *newPkg);
        if (pkg != newPkg) {
            chirData.AppendNewPackage(newPkg);
        }
    }
    return true;
}
}