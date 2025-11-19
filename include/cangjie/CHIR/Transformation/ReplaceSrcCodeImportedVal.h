// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_REPLACE_SRC_CODE_IMPORTED_VAL_H
#define CANGJIE_CHIR_REPLACE_SRC_CODE_IMPORTED_VAL_H

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Package.h"

namespace Cangjie::CHIR {
class ReplaceSrcCodeImportedVal {
public:
    ReplaceSrcCodeImportedVal(
        Package& package, std::unordered_map<std::string, FuncBase*>& implicitFuncs, CHIRBuilder& builder);
    void Run(const std::unordered_set<Func*>& srcCodeImportedFuncs,
        const std::unordered_set<GlobalVar*>& srcCodeImportedVars,
        const std::unordered_set<ClassDef*>& uselessClasses,
        const std::unordered_set<Func*>& uselessLambda);

private:
    void CreateSrcImpotedValueSymbol(const std::unordered_set<Func*>& srcCodeImportedFuncs,
        const std::unordered_set<GlobalVar*>& srcCodeImportedVars);
    void CreateSrcImportedFuncSymbol(Func& fn);
    void CreateSrcImportedVarSymbol(GlobalVar& gv);
    std::unordered_set<Func*> RemoveUselessDefFromCC(
        const std::unordered_set<ClassDef*>& uselessClasses, const std::unordered_set<Func*>& uselessLambda);
    void ReplaceSrcCodeImportedFuncUsers(std::unordered_set<Func*>& toBeRemovedFuncs,
        std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>>& replaceTable);
    void ReplaceSrcCodeImportedVarUsers(
        std::unordered_set<Func*>& toBeRemovedFuncs, std::unordered_set<GlobalVar*>& toBeRemovedVars,
        std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>>& replaceTable);

private:
    Package& package;
    std::unordered_map<std::string, FuncBase*>& implicitFuncs;
    CHIRBuilder& builder;

    std::unordered_map<Func*, ImportedFunc*> srcCodeImportedFuncMap;
    std::unordered_map<GlobalVar*, ImportedVar*> srcCodeImportedVarMap;
};
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_REPLACE_SRC_CODE_IMPORTED_VAL_H