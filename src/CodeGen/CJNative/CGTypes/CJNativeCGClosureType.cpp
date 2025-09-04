// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/CGTypes/CGClosureType.h"

#include "llvm/IR/DerivedTypes.h"

#include "CGModule.h"
#include "Utils/CGUtils.h"

namespace Cangjie {
namespace CodeGen {
std::string CGClosureType::GetTypeNameByClosureType([[maybe_unused]] const CHIR::ClosureType& closureType)
{
    return GetMangledNameOfCompilerAddedClass("Closure");
}

llvm::Type* CGClosureType::GenLLVMType()
{
    return llvm::Type::getInt8PtrTy(cgCtx.GetLLVMContext(), 1U);
}

llvm::Constant* CGClosureType::GenSourceGenericOfTypeInfo()
{
    return CGType::GenSourceGenericOfTypeInfo();
}

llvm::Constant* CGClosureType::GenTypeArgsNumOfTypeInfo()
{
    auto funcType = static_cast<const CHIR::ClosureType&>(chirType).GetFuncType();
    return llvm::ConstantInt::get(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()), funcType->GetTypeArgs().size());
}

llvm::Constant* CGClosureType::GenTypeArgsOfTypeInfo()
{
    auto funcType = static_cast<const CHIR::ClosureType&>(chirType).GetFuncType();
    auto typeInfoPtrTy = CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext());

    std::vector<llvm::Constant*> constants;
    (void)constants.emplace_back(CGType::GetOrCreate(cgMod, DeRef(*funcType->GetReturnType()))->GetOrCreateTypeInfo());
    for (auto paramType : funcType->GetParamTypes()) {
        (void)constants.emplace_back(CGType::GetOrCreate(cgMod, DeRef(*paramType))->GetOrCreateTypeInfo());
    }

    auto typeOfGenericArgsGV = llvm::ArrayType::get(typeInfoPtrTy, constants.size());
    auto typeInfoOfGenericArgs = llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(
        CGType::GetNameOfTypeInfoGV(chirType) + ".typeArgs", typeOfGenericArgsGV));
    typeInfoOfGenericArgs->setInitializer(llvm::ConstantArray::get(typeOfGenericArgsGV, constants));
    typeInfoOfGenericArgs->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
    typeInfoOfGenericArgs->addAttribute(CJTI_TYPE_ARGS_ATTR);
    return llvm::ConstantExpr::getBitCast(typeInfoOfGenericArgs, llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
}

void CGClosureType::GenContainedCGTypes()
{
    auto& closureType = StaticCast<const CHIR::ClosureType&>(chirType);
    containedCGTypes = {CGType::GetOrCreate(cgMod, CGType::GetRefTypeOfCHIRInt8(cgCtx.GetCHIRBuilder())),
        CGType::GetOrCreate(cgMod, closureType.GetEnvType())};
}

void CGClosureType::CalculateSizeAndAlign()
{
    llvm::DataLayout layOut = cgMod.GetLLVMModule()->getDataLayout();
    size = layOut.getTypeAllocSize(llvmType);
    align = layOut.getABITypeAlignment(llvmType);
}
} // namespace CodeGen
} // namespace Cangjie
