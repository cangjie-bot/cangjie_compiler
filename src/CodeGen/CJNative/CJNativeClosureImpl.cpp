// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "CJNative/CJNativeClosure.h"

#include <optional>

#include "Base/TupleExprImpl.h"
#include "CGModule.h"
#include "CJNative/CGTypes/CGExtensionDef.h"
#include "IRBuilder.h"
#include "Utils/BlockScopeImpl.h"
#include "Utils/CGUtils.h"

#include "cangjie/CHIR/Expression.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"

using namespace Cangjie;
using namespace CodeGen;

llvm::Value* CodeGen::GenerateClosure(IRBuilder2& irBuilder, const CHIR::Tuple& tuple)
{
    auto chirType = tuple.GetResult()->GetType();
    CJC_ASSERT(chirType->IsClosure() && "Should not reach here.");
    auto i8PtrTy = irBuilder.getInt8PtrTy();
    auto& cgMod = irBuilder.GetCGModule();
    auto args = tuple.GetOperands();
    auto lambdaObj = **(cgMod | args[1]);
    if (auto funcItem = args[0]; !funcItem->GetUsers()[0]->IsConstantNull()) {
        auto funcField = irBuilder.GetPayloadFromObject(lambdaObj);
        auto functionPtr = **(cgMod | funcItem);
        (void)irBuilder.CreateStore(irBuilder.CreateBitCast(functionPtr, i8PtrTy), funcField);
    }
    // Construct the inheritance relationship between `lambdaObj` and `Func.tt`.
    auto envType =
        DeRef(*StaticCast<CHIR::TypeCast*>(StaticCast<CHIR::LocalVar*>(args[1])->GetExpr())->GetSourceTy());
    auto content = CGExtensionDef::GetEmptyExtensionDefContent(cgMod, *envType);
    auto& llvmCtx = cgMod.GetLLVMContext();
    std::vector<llvm::Type*> argTypes{
        llvm::Type::getInt32Ty(llvmCtx), CGType::GetOrCreateTypeInfoPtrType(llvmCtx)->getPointerTo()};
    auto extendDefName =
        StaticCast<CHIR::CustomType*>(envType)->GetCustomTypeDef()->GetIdentifierWithoutPrefix() + "_ed_Func";
    llvm::FunctionType* interfaceFnType = llvm::FunctionType::get(i8PtrTy, argTypes, false);
    llvm::Function* interfaceFn = llvm::cast<llvm::Function>(
        cgMod.GetLLVMModule()->getOrInsertFunction(extendDefName + "_iFn", interfaceFnType).getCallee());
    SetGCCangjie(interfaceFn);
    if (interfaceFn->empty()) {
        interfaceFn->setLinkage(llvm::Function::PrivateLinkage);
        interfaceFn->addFnAttr("native-interface-fn");
        CodeGen::IRBuilder2 irBuilder(cgMod);
        auto entryBB = irBuilder.CreateEntryBasicBlock(interfaceFn, "entry");
        irBuilder.SetInsertPoint(entryBB);
        std::unordered_map<const CHIR::Type*, std::function<llvm::Value*(IRBuilder2&)>> genericMap;
        size_t idx = 0;
        auto typeinfos = interfaceFn->getArg(1);
        for (auto typeArg : StaticCast<const CHIR::ClassType*>(envType)->GetGenericArgs()) {
            genericMap.emplace(typeArg,
                [idx, typeinfos](IRBuilder2& irBuilder) { return irBuilder.GetTypeInfoFromTiArray(typeinfos, idx); });
            ++idx;
        }
        irBuilder.CreateRet(irBuilder.CreateBitCast(irBuilder.CreateTypeInfo(*chirType, genericMap), i8PtrTy));

        content[static_cast<size_t>(INTERFACE_FN_OR_INTERFACE_TI)] =
            llvm::ConstantExpr::getBitCast(interfaceFn, i8PtrTy);
        CGExtensionDef::CreateExtensionDefForType(cgMod, extendDefName, content);
    }
    return lambdaObj;
}
