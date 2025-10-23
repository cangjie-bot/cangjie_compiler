// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/MetaTransformation/CangjieMetaTransform.h"
#include "cangjie/CHIR/CHIR.h"
#include "cangjie/CHIR/Serializer/CHIRSerializer.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"

namespace Cangjie {
namespace {
using namespace CHIR;
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

Package* Run(CHIRContext& ctx, const Package& p, const std::pair<std::string, HANDLE>& handle)
{
    auto fb = CHIRSerializer::Serialize(p, ToCHIR::Phase::RAW);
    auto invokeC = reinterpret_cast<void* (*)(void(*)(Memory*), Memory*)>(RuntimeInit::GetInstance().runtimeMethodFunc);
    auto initPlugin = reinterpret_cast<void (*)(const char*)>(RuntimeInit::GetInstance().initLibFunc);
    initPlugin(handle.first.c_str());
    auto fn = reinterpret_cast<void (*)(Memory*)>(handle.second);
    auto pkg = fb.Data(); // need not release, as this memory is managed by fb
    auto tmp = new(std::nothrow) Memory{pkg, fb.Size()};
    if (!tmp) {
        Errorln("Failed to allocate memory in meta transform");
        return nullptr;
    }
    auto cjThreadHandle = invokeC(fn, tmp);
    auto getRetFunc = reinterpret_cast<int (*)(const void*, void**)>(RuntimeInit::GetInstance().getRet);
    void* retPtr{};
    int retCode = getRetFunc(cjThreadHandle, &retPtr);
    if (retCode != 0) {
        Errorln("CHIR plugin execution failed with code ", retCode);
        free(tmp);
        return nullptr;
    }
    CHIRBuilder b{ctx};
    MyDeserialize myd(b, *tmp);
    auto ret = myd.Deserialize();

    if (pkg != tmp->ptr) {
        free(tmp->ptr);
    }
    free(tmp);
    
    return ret;
}

void MergePackage(Package& old, Package& n)
{
    std::unordered_map<std::string_view, std::pair<GlobalVar*, size_t>> oldGvMap;
    auto oldGvs = old.GetGlobalVars();
    for (size_t i = 0; i < oldGvs.size(); ++i) {
        oldGvMap[oldGvs[i]->GetIdentifier()] = {oldGvs[i], i};
    }
    for (auto & gv : n.GetGlobalVars()) {
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
    RuntimeInit::GetInstance().InitRuntime(
        invocation.GetRuntimeLibPath(), invocation.globalOptions.environment.allVariables);
    auto pkg = chirData.GetCurrentCHIRPackage();
    for (auto &plugin : cangjieCHIRPlugins) {
        auto newPkg = Run(chirData.GetCHIRContext(), *pkg, plugin);
        if (!newPkg) {
            Errorln("run CHIR plugin returned null package");
            return false;
        }
        MergePackage(*pkg, *newPkg);
        chirData.AppendNewPackage(newPkg);
    }
    return true;
}
}