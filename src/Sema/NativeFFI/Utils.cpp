// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Utils.h"
#include "TypeCheckUtil.h"

#include "Desugar/AfterTypeCheck.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"

namespace Cangjie::Native::FFI {

using namespace TypeCheckUtil;

OwnedPtr<RefExpr> CreateThisRef(Ptr<Decl> target, Ptr<Ty> ty, Ptr<File> curFile)
{
    auto thisRef = MakeOwned<RefExpr>();
    thisRef->isThis = true;
    thisRef->ty = ty;
    thisRef->ref.identifier = SrcIdentifier("this");
    thisRef->ref.target = target;
    thisRef->curFile = curFile;
    return thisRef;
}

OwnedPtr<CallExpr> CreateThisCall(Decl& target, FuncDecl& baseTarget, Ptr<Ty> funcTy, Ptr<File> curFile)
{
    auto call = CreateCallExpr(CreateThisRef(Ptr(&baseTarget), funcTy, curFile), {});
    call->callKind = CallKind::CALL_OBJECT_CREATION;
    call->ty = target.ty;
    call->resolvedFunction = Ptr(&baseTarget);

    return call;
}

OwnedPtr<PrimitiveType> CreateUnitType(Ptr<File> curFile)
{
    auto ret = MakeOwned<PrimitiveType>();
    ret->str = "Unit";
    ret->kind = TypeKind::TYPE_UNIT;
    ret->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    ret->curFile = curFile;

    return ret;
}

std::vector<Ptr<Ty>> GetParamTys(FuncParamList& params)
{
    std::vector<Ptr<Ty>> paramTys;

    for (auto& param : params.params) {
        paramTys.push_back(param->ty);
    }
    return paramTys;
}

OwnedPtr<RefExpr> CreateSuperRef(Ptr<Decl> target, Ptr<Ty> ty)
{
    auto superRef = MakeOwned<RefExpr>();
    superRef->isSuper = true;
    superRef->ty = ty;
    superRef->ref.identifier = SrcIdentifier("super");
    superRef->ref.target = target;
    return superRef;
}

OwnedPtr<CallExpr> CreateSuperCall(Decl& target, FuncDecl& baseTarget, Ptr<Ty> funcTy)
{
    auto call = CreateCallExpr(CreateSuperRef(Ptr(&baseTarget), funcTy), {});
    call->callKind = CallKind::CALL_SUPER_FUNCTION;
    call->ty = target.ty;
    call->resolvedFunction = Ptr(&baseTarget);

    return call;
}

OwnedPtr<Type> CreateType(Ptr<Ty> ty)
{
    auto res = MakeOwned<Type>();
    res->ty = ty;
    return res;
}

OwnedPtr<Type> CreateFuncType(Ptr<FuncTy> ty)
{
    auto res = MakeOwned<FuncType>();
    res->ty = ty;

    for (auto param : ty->paramTys) {
        res->paramTypes.push_back(CreateType(param));
    }

    return res;
}

OwnedPtr<Expr> CreateBoolMatch(
    OwnedPtr<Expr> selector, OwnedPtr<Expr> trueBranch, OwnedPtr<Expr> falseBranch, Ptr<Ty> ty)
{
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    OwnedPtr<ConstPattern> truePattern = MakeOwned<ConstPattern>();
    truePattern->literal = CreateLitConstExpr(LitConstKind::BOOL, "true", BOOL_TY);
    truePattern->ty = BOOL_TY;

    OwnedPtr<ConstPattern> falsePattern = MakeOwned<ConstPattern>();
    falsePattern->literal = CreateLitConstExpr(LitConstKind::BOOL, "false", BOOL_TY);
    falsePattern->ty = BOOL_TY;

    auto caseTrue = CreateMatchCase(std::move(truePattern), std::move(trueBranch));
    auto caseFalse = CreateMatchCase(std::move(falsePattern), std::move(falseBranch));

    std::vector<OwnedPtr<MatchCase>> matchCases;
    matchCases.emplace_back(std::move(caseTrue));
    matchCases.emplace_back(std::move(caseFalse));
    auto curFile = selector->curFile;
    return WithinFile(CreateMatchExpr(std::move(selector), std::move(matchCases), ty), curFile);
}

StructDecl& GetStringDecl(const ImportManager& importManager)
{
    static auto decl = importManager.GetCoreDecl<StructDecl>(STD_LIB_STRING);
    CJC_NULLPTR_CHECK(decl);
    return *decl;
}

OwnedPtr<CallExpr> WrapReturningLambdaCall(TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes)
{
    auto retTy = nodes.back()->ty;
    auto lambda = WrapReturningLambdaExpr(typeManager, std::move(nodes));
    return CreateCallExpr(std::move(lambda), {}, nullptr, retTy);
}

OwnedPtr<LambdaExpr> WrapReturningLambdaExpr(TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes, std::vector<OwnedPtr<FuncParam>> lambdaParams)
{
    auto curFile = nodes[0]->curFile;
    CJC_ASSERT(!nodes.empty());
    std::vector<Ptr<Ty>> lambdaParamTys;
    std::transform(
        lambdaParams.begin(), lambdaParams.end(), std::back_inserter(lambdaParamTys), [](auto& p) { return p->ty; });
    auto paramLists = Nodes<FuncParamList>(CreateFuncParamList(std::move(lambdaParams)));
    auto retTy = nodes.back()->ty;
    auto unsafeBlock = CreateBlock(Nodes(ASTCloner::Clone(Ptr(As<ASTKind::EXPR>(nodes.back().get())))), retTy);
    unsafeBlock->EnableAttr(Attribute::UNSAFE);
    auto retExpr = CreateReturnExpr(std::move(unsafeBlock));
    retExpr->ty = TypeManager::GetNothingTy();
    nodes.pop_back();
    auto lambda = CreateLambdaExpr(
        CreateFuncBody(
            std::move(paramLists),
            nullptr,
            CreateBlock(std::move(nodes), retTy),
            retTy));
    retExpr->refFuncBody = lambda->funcBody.get();
    lambda->funcBody->body->body.push_back(std::move(retExpr));
    lambda->curFile = curFile;
    lambda->ty = typeManager.GetFunctionTy(std::move(lambdaParamTys), retTy);
    return lambda;
}

std::string GetCangjieLibName(const std::string& outputLibPath, const std::string& fullPackageName, bool trimmed)
{
    if (FileUtil::IsDir(outputLibPath)) {
        return fullPackageName;
    }
    auto outputFileName = FileUtil::GetFileName(outputLibPath);

    constexpr std::string_view libPrefix = "lib";
    // check if [outputLibPath] starts with [LIB_PREFIX]
    if (outputFileName.rfind(libPrefix, 0) == 0) {
        if (!trimmed) {
            return outputFileName;
        }

        size_t extIdx = outputFileName.find_last_of(".");
        if (extIdx == std::string::npos) {
            return fullPackageName;
        }
        return outputFileName.substr(libPrefix.size(), extIdx - libPrefix.size());
    }
    return fullPackageName;
}

std::string GetMangledMethodName(const BaseMangler& mangler,
    const std::vector<OwnedPtr<FuncParam>>& params, const std::string& methodName, GenericConfigInfo* genericConfig)
{
    std::string name(methodName);

    for (auto& param : params) {
        auto paramTy = param->ty;
        if (genericConfig && param->ty->HasGeneric()) {
            paramTy = GetGenericInstTy(genericConfig, param->ty->name);
        }
        std::string mangledParam = mangler.MangleType(*paramTy);
        std::replace(mangledParam.begin(), mangledParam.end(), '.', '_');
        name += mangledParam;
    }

    return name;
}

Ptr<Annotation> GetForeignNameAnnotation(const Decl& decl)
{
    auto it = std::find_if(decl.annotations.begin(), decl.annotations.end(),
        [](const auto& anno) { return anno->kind == AnnotationKind::FOREIGN_NAME; });
    return it != decl.annotations.end() ? it->get() : nullptr;
}

bool IsSuperConstructorCall(const CallExpr& call)
{
    auto baseFunc = As<ASTKind::REF_EXPR>(call.baseFunc.get());
    if (!baseFunc || !baseFunc->isSuper) {
        return false;
    }
    return call.callKind == CallKind::CALL_SUPER_FUNCTION;
}

Ptr<Annotation> GetAnnotation(const Decl& decl, AnnotationKind annotationKind)
{
    auto it = std::find_if(decl.annotations.begin(), decl.annotations.end(),
        [annotationKind](const auto& anno) { return anno->kind == annotationKind; });
    return it != decl.annotations.end() ? it->get() : nullptr;
}

Ptr<std::string> GetSingleArgumentAnnotationValue(const Decl& target, AnnotationKind annotationKind)
{
    for (auto& anno : target.annotations) {
        if (anno->kind != annotationKind) {
            continue;
        }

        CJC_ASSERT(anno->args.size() == 1);
        if (anno->args.empty()) {
            break;
        }

        CJC_ASSERT(anno->args[0]->expr->astKind == ASTKind::LIT_CONST_EXPR);
        auto lce = As<ASTKind::LIT_CONST_EXPR>(anno->args[0]->expr.get());
        CJC_ASSERT(lce);

        return &lce->stringValue;
    }

    return nullptr;
}

OwnedPtr<PrimitiveType> GetPrimitiveType(std::string typeName, AST::TypeKind typekind) {
    OwnedPtr<PrimitiveType> type = MakeOwned<PrimitiveType>();
    type->str = typeName;
    type->kind = typekind;
    type->ty = TypeManager::GetPrimitiveTy(typekind);
    return type;
}

bool IsCJMappingGeneric(const Decl& decl) {
    auto classDecl = DynamicCast<ClassDecl*>(&decl);
    if (classDecl && !classDecl->TestAnyAttr(AST::Attribute::ABSTRACT, AST::Attribute::OPEN) &&
        classDecl->ty->HasGeneric()) {
        return true;
    }

    auto structDecl = DynamicCast<StructDecl*>(&decl);
    if (structDecl && structDecl->ty->HasGeneric()) {
        return true;
    }

    auto enumDecl = DynamicCast<EnumDecl*>(&decl);
    if (enumDecl && enumDecl->ty->HasGeneric()) {
        return true;
    }

    auto interfaceDecl = DynamicCast<InterfaceDecl*>(&decl);
    if (interfaceDecl && interfaceDecl->ty->HasGeneric()) {
        return true;
    }
    return false;
}

void SplitAndTrim(std::string str, std::vector<std::string>& types)
{
    size_t pos = str.find(',');
    if (pos == std::string::npos) {
        types.push_back(str);
        return;
    }
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        types.push_back(token);
    }
}

std::string JoinVector(const std::vector<std::string>& vec, const std::string& delimiter)
{
    std::string result;
    for (size_t i = 0; i < vec.size(); ++i) {
        result += vec[i];
        if (i != vec.size() - 1) {
            result += delimiter;
        }
    }
    return result;
}

void InitGenericConfigs(
    const File& file, const AST::Decl* decl, std::vector<GenericConfigInfo*>& genericConfigs, bool& isGenericGlueCode)
{
    // Collect information on the names of generic configuration methods
    // such as: {GenericClass<int32>, symbols: ["find", "value"]>}
    std::unordered_map<std::string, std::unordered_set<std::string>> visibleFuncs;
    for (const auto& outerPair : file.curPackage->allowedInteropCJGenericInstantiations) {
        const auto declSymbolName = outerPair.first;
        const auto& innerMap = outerPair.second;
        if (declSymbolName != decl->identifier.Val()) {
            continue;
        }
        for (const auto& innerPair : innerMap) {
            const std::string typeStr = innerPair.first;
            const GenericTypeArguments& args = innerPair.second;
            std::unordered_set<std::string> funcNames = args.symbols;
            std::vector<std::string> actualTypes;
            SplitAndTrim(typeStr, actualTypes);
            std::vector<std::pair<std::string, std::string>> instTypes;
            const auto typeArgs = decl->ty->typeArgs;
            for (size_t i = 0; i < typeArgs.size(); i++) {
                instTypes.push_back(std::make_pair(typeArgs[i]->name, actualTypes[i]));
            }
            std::string declName = decl->identifier.Val();
            std::string declWInstStr = declName + JoinVector(actualTypes);
            GenericConfigInfo* declGenericConfig = new GenericConfigInfo(declName, declWInstStr, instTypes, funcNames);
            genericConfigs.push_back(declGenericConfig);
            if (!isGenericGlueCode) {
                isGenericGlueCode = true;
            }
        }
    }
}

std::string GetGenericActualType(const GenericConfigInfo* config, std::string genericName)
{
    CJC_ASSERT(config);
    for (size_t i = 0; i < config->instTypes.size(); ++i) {
        if (config->instTypes[i].first == genericName) {
            std::string instType = config->instTypes[i].second;
            return instType;
        }
    }
    return "";
}

// Current generic just support primitive type
TypeKind GetGenericActualTypeKind(std::string configType) {
    static const std::unordered_map<std::string, TypeKind> typeMap = {
        {"Int8", TypeKind::TYPE_INT8},
        {"Int16", TypeKind::TYPE_INT16},
        {"Int32", TypeKind::TYPE_INT32},
        {"Int64", TypeKind::TYPE_INT64},
        {"IntNative", TypeKind::TYPE_INT_NATIVE},
        {"UInt8", TypeKind::TYPE_UINT8},
        {"UInt16", TypeKind::TYPE_UINT16},
        {"UInt32", TypeKind::TYPE_UINT32},
        {"UInt64", TypeKind::TYPE_UINT64},
        {"UIntNative", TypeKind::TYPE_UINT_NATIVE},
        {"Float16", TypeKind::TYPE_FLOAT16},
        {"Float32", TypeKind::TYPE_FLOAT32},
        {"Float64", TypeKind::TYPE_FLOAT64},
        {"Bool", TypeKind::TYPE_BOOLEAN},
    };
    auto it = typeMap.find(configType);
    CJC_ASSERT(it != typeMap.end());
    return it->second;
}

Ptr<Ty> GetGenericInstTy(GenericConfigInfo* config, std::string genericName) {
    auto actualTypeName = GetGenericActualType(config, genericName);
    return GetGenericInstTy(actualTypeName);
}

Ptr<Ty> GetGenericInstTy(std::string typeStr) {
    auto typeKind = GetGenericActualTypeKind(typeStr);
    // Current only support primitive type.
    auto ty = TypeManager::GetPrimitiveTy(typeKind);
    return ty;
}

OwnedPtr<Type> GetGenericInstType(const GenericConfigInfo* config, std::string genericName) {
    auto actualTypeName = GetGenericActualType(config, genericName);
    return GetGenericInstType(actualTypeName);
}

OwnedPtr<Type> GetGenericInstType(std::string typeStr) {
    auto typeKind = GetGenericActualTypeKind(typeStr);
    // Current only support primitive type.
    auto type = GetPrimitiveType(typeStr, typeKind);
    return type;
}

// Match generic parameters in all function parameters to their corresponding Ptr<Ty>.
void GetArgsAndRetGenericActualTyVector(const GenericConfigInfo* config, FuncDecl& ctor,
    const std::vector<std::pair<std::string, std::string>> instTypes,
    std::unordered_map<std::string, Ptr<Ty>> &actualTyArgMap, std::vector<Ptr<Ty>> &funcTyParams,
    std::vector<OwnedPtr<Type>> &actualPrimitiveType)
{
    Ptr<Ty> retTy = StaticCast<FuncTy*>(ctor.ty)->retTy;
    // Lambda function to handle generic type replacement.
    auto replaceGenericType = [&](const std::string& typeName) -> Ptr<Ty> {
        if (!typeName.empty()) {
            auto it = std::find_if(instTypes.begin(), instTypes.end(),
                [&typeName](const std::pair<std::string, std::string>& p) {
                    return p.first == typeName;
                });
            if (it != instTypes.end()) {
                auto actualTypeKind = GetGenericActualTypeKind(it->second);
                Ptr<Ty> actualTy = TypeManager::GetPrimitiveTy(actualTypeKind);
                actualTy->name = typeName;
                actualTyArgMap[typeName] = actualTy;
                return actualTy;
            }
        }
        return nullptr;
    };

    // Analyze the generic parameters of class/struct/enum/interface.
    if (ctor.outerDecl) {
        for (auto argTy : ctor.outerDecl->ty->typeArgs) {
            if (argTy->IsGeneric()) {
                replaceGenericType(argTy->name);
            }
        }
    }

    // Analyze generic parameters within inner functions.
    for (size_t argIdx = 0; argIdx < ctor.funcBody->paramLists[0]->params.size(); ++argIdx) {
        auto& arg = ctor.funcBody->paramLists[0]->params[argIdx];
        if (arg->ty->IsGeneric()) {
            if (auto actualTy = replaceGenericType(arg->ty->name)) {
                funcTyParams.emplace_back(actualTy);
                actualPrimitiveType.emplace_back(GetGenericInstType(config, actualTy->name));
            } else {
                funcTyParams.emplace_back(arg->ty);
            }
        } else {
            funcTyParams.emplace_back(arg->ty);
        }
    }

    // Analyze generic retType parameters within inner functions.
    if (retTy->IsGeneric()) {
        auto actualRetTy = replaceGenericType(retTy->name);
        actualPrimitiveType.emplace_back(GetGenericInstType(config, actualRetTy->name));
    }

}

Ptr<Ty> GetInstantyForGenericTy(Decl& decl, const std::unordered_map<std::string, Ptr<Ty>> &actualTyArgMap, TypeManager& typeManager)
{
    std::vector<Ptr<Ty>> actualTypeArgs;
    for (const auto& typeArg : decl.ty->typeArgs) {
        std::string typeArgName = typeArg->name;

        auto it = actualTyArgMap.find(typeArgName);
        if (it != actualTyArgMap.end()) {
            actualTypeArgs.emplace_back(it->second);
        }
    }

    Ptr<Ty> instantTy;
    auto classDecl = As<ASTKind::CLASS_DECL>(&decl);
    if (classDecl) {
        instantTy = typeManager.GetClassTy(*classDecl, actualTypeArgs);
    }
    auto structDecl = As<ASTKind::STRUCT_DECL>(&decl);
    if (structDecl) {
        instantTy = typeManager.GetStructTy(*structDecl, actualTypeArgs);
    }
    auto enumDecl = As<ASTKind::ENUM_DECL>(&decl);
    if (enumDecl) {
        instantTy = typeManager.GetEnumTy(*enumDecl, actualTypeArgs);
    }
    auto interfaceDecl = As<ASTKind::INTERFACE_DECL>(&decl);
    if (interfaceDecl) {
        instantTy = typeManager.GetInterfaceTy(*interfaceDecl, actualTypeArgs);
    }
    return instantTy;
}

bool IsGenericParam(const Ptr<Ty> ty, const Ptr<AST::Decl> decl, Native::FFI::GenericConfigInfo* genericConfig) {
    return IsCJMappingGeneric(*decl) && ty->kind == Cangjie::AST::TypeKind::TYPE_GENERICS &&
        !GetGenericActualType(genericConfig, ty->name).empty();
}

bool IsVisibalFunc(const FuncDecl& funcDecl, const Ptr<AST::Decl> decl, Native::FFI::GenericConfigInfo* genericConfig)
{
    bool hasGenericParm = false;
    auto& params = funcDecl.funcBody->paramLists[0]->params;
    auto& retType = funcDecl.funcBody->retType;
    for (auto& param : params) {
        if (IsGenericParam(param->type->ty, decl, genericConfig)) {
            hasGenericParm = true;
            break;
        }
    }
    if (!hasGenericParm) {
        hasGenericParm = IsGenericParam(retType->ty, decl, genericConfig);
    }

    if (!hasGenericParm) {
        return true;
    }

    bool isVisibalFunc = genericConfig->funcNames.count(funcDecl.identifier.Val()) > 0;
    return hasGenericParm && isVisibalFunc;
}

} // namespace Cangjie::Native::FFI
