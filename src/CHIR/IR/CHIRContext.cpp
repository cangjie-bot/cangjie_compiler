// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the CHIRContext class in CHIR.
 */

#include "cangjie/CHIR/IR/CHIRContext.h"

#include <thread>
#include "cangjie/Basic/Print.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/ExtendDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/CHIR/IR/Value/Value.h"

using namespace Cangjie::CHIR;

namespace {
const int ALLOCATED_VALUES_START_IDX = 0;
const int ALLOCATED_VALUES_END_IDX = 1;
const int ALLOCATED_EXPRS_START_IDX = 2;
const int ALLOCATED_EXPRS_END_IDX = 3;
const int ALLOCATED_BLOCKGROUPS_START_IDX = 4;
const int ALLOCATED_BLOCKGROUPS_END_IDX = 5;
const int ALLOCATED_BLOCKS_START_IDX = 6;
const int ALLOCATED_BLOCKS_END_IDX = 7;
const int ALLOCATED_STRUCTS_START_IDX = 8;
const int ALLOCATED_STRUCTS_END_IDX = 9;
const int ALLOCATED_CLASSES_START_IDX = 10;
const int ALLOCATED_CLASSES_END_IDX = 11;
const int ALLOCATED_ENUMS_START_IDX = 12;
const int ALLOCATED_ENUMS_END_IDX = 13;
}

std::mutex CHIRContext::dynamicAllocatedTysMtx;
size_t TypePtrHash::operator()(const Type* ptr) const
{
    return ptr != nullptr ? ptr->Hash() : 0;
}

bool TypePtrEqual::operator()(const Type* ptr1, const Type* ptr2) const
{
    if (ptr1 == nullptr && ptr2 == nullptr) {
        return true;
    }
    return ptr1 != nullptr && ptr2 != nullptr && *ptr1 == *ptr2;
}

void CHIRContext::DeleteAllocatedInstance(std::vector<size_t>& idxs)
{
    // Delete the allocated instances.
    for (size_t i = idxs[ALLOCATED_VALUES_START_IDX]; i < idxs[ALLOCATED_VALUES_END_IDX]; i++) {
        delete allocatedValues[i];
    }
    for (size_t i = idxs[ALLOCATED_EXPRS_START_IDX]; i < idxs[ALLOCATED_EXPRS_END_IDX]; i++) {
        delete allocatedExprs[i];
    }
    for (size_t i = idxs[ALLOCATED_BLOCKGROUPS_START_IDX]; i < idxs[ALLOCATED_BLOCKGROUPS_END_IDX]; i++) {
        delete allocatedBlockGroups[i];
    }
    for (size_t i = idxs[ALLOCATED_BLOCKS_START_IDX]; i < idxs[ALLOCATED_BLOCKS_END_IDX]; i++) {
        delete allocatedBlocks[i];
    }
    for (size_t i = idxs[ALLOCATED_STRUCTS_START_IDX]; i < idxs[ALLOCATED_STRUCTS_END_IDX]; i++) {
        delete allocatedStructs[i];
    }
    for (size_t i = idxs[ALLOCATED_CLASSES_START_IDX]; i < idxs[ALLOCATED_CLASSES_END_IDX]; i++) {
        delete allocatedClasses[i];
    }
    for (size_t i = idxs[ALLOCATED_ENUMS_START_IDX]; i < idxs[ALLOCATED_ENUMS_END_IDX]; i++) {
        delete allocatedEnums[i];
    }
}

void CHIRContext::DeleteAllocatedTys()
{
    for (auto inst : std::as_const(this->dynamicAllocatedTys)) {
        delete inst;
    }
    this->dynamicAllocatedTys.clear();

    for (auto inst : std::as_const(this->constAllocatedTys)) {
        delete inst;
    }
    this->constAllocatedTys.clear();

    for (auto inst : std::as_const(this->allocatedExtends)) {
        delete inst;
    }
    this->allocatedExtends.clear();

    if (this->curPackage != nullptr) {
        delete this->curPackage;
        this->curPackage = nullptr;
    }
}

Package* CHIRContext::GetCurPackage() const
{
    return this->curPackage;
}

void CHIRContext::SetCurPackage(Package* pkg)
{
    this->curPackage = pkg;
}

// Tasks are evenly distributed to obtain the start and end subscripts of the data to be processed by each thread.
static void DivideArray(size_t len, size_t threadNum, std::vector<std::vector<size_t>>& indexs)
{
    size_t size = len / threadNum;
    size_t remainder = len % threadNum;
    size_t start = 0;
    size_t end = 0;
    for (size_t i = 0; i < threadNum; i++) {
        start = end;
        end = start + size;
        if (remainder > 0) {
            end++;
            remainder--;
        }
        indexs[i].push_back(start);
        indexs[i].push_back(end);
    }
    return;
}

CHIRContext::CHIRContext(std::unordered_map<unsigned int, std::string>* fnMap, size_t threadsNum)
    : curPackage(nullptr), fileNameMap(fnMap), threadsNum(threadsNum)
{
    unitTy = GetType<UnitType>(ModalInfo{});
    boolTy = GetType<BooleanType>(ModalInfo{});
    runeTy = GetType<RuneType>(ModalInfo{});
    nothingTy = GetType<NothingType>(ModalInfo{});
    int8Ty = GetType<IntType>(Type::TypeKind::TYPE_INT8, ModalInfo{});
    int16Ty = GetType<IntType>(Type::TypeKind::TYPE_INT16, ModalInfo{});
    int32Ty = GetType<IntType>(Type::TypeKind::TYPE_INT32, ModalInfo{});
    int64Ty = GetType<IntType>(Type::TypeKind::TYPE_INT64, ModalInfo{});
    intNativeTy = GetType<IntType>(Type::TypeKind::TYPE_INT_NATIVE, ModalInfo{});
    uint8Ty = GetType<IntType>(Type::TypeKind::TYPE_UINT8, ModalInfo{});
    uint16Ty = GetType<IntType>(Type::TypeKind::TYPE_UINT16, ModalInfo{});
    uint32Ty = GetType<IntType>(Type::TypeKind::TYPE_UINT32, ModalInfo{});
    uint64Ty = GetType<IntType>(Type::TypeKind::TYPE_UINT64, ModalInfo{});
    uintNativeTy = GetType<IntType>(Type::TypeKind::TYPE_UINT_NATIVE, ModalInfo{});
    float16Ty = GetType<FloatType>(Type::TypeKind::TYPE_FLOAT16, ModalInfo{});
    float32Ty = GetType<FloatType>(Type::TypeKind::TYPE_FLOAT32, ModalInfo{});
    float64Ty = GetType<FloatType>(Type::TypeKind::TYPE_FLOAT64, ModalInfo{});
    for (int i = 0; i < MODAL_INFO_COUNT; i++) {
        cstringTy[i] = GetType<CStringType>(AST::ToModalInfo(i));
    }
    voidTy = GetType<VoidType>(ModalInfo{});
}
 
CHIRContext::~CHIRContext()
{
    if (threadsNum == 1) {
        std::vector<size_t> indexs{0, allocatedValues.size(), 0, allocatedExprs.size(), 0, allocatedBlockGroups.size(),
            0, allocatedBlocks.size(), 0, allocatedStructs.size(), 0, allocatedClasses.size(), 0,
            allocatedEnums.size()};
        DeleteAllocatedInstance(indexs);
        DeleteAllocatedTys();
    } else {
        // Delete the allocated instances.
        std::vector<std::thread> threads;
        threads.reserve(threadsNum);
        std::vector<std::vector<size_t>> indexs(threadsNum, std::vector<size_t>());
        DivideArray(allocatedValues.size(), threadsNum - 1, indexs);
        DivideArray(allocatedExprs.size(), threadsNum - 1, indexs);
        DivideArray(allocatedBlockGroups.size(), threadsNum - 1, indexs);
        DivideArray(allocatedBlocks.size(), threadsNum - 1, indexs);
        DivideArray(allocatedStructs.size(), threadsNum - 1, indexs);
        DivideArray(allocatedClasses.size(), threadsNum - 1, indexs);
        DivideArray(allocatedEnums.size(), threadsNum - 1, indexs);
        for (size_t i = 0; i < threadsNum - 1; i++) {
            std::vector<size_t>& idxs = indexs[i];
            threads.emplace_back([&idxs, this]() { DeleteAllocatedInstance(idxs); });
        }
        threads.emplace_back([this]() { DeleteAllocatedTys(); });
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    allocatedExprs.clear();
    allocatedValues.clear();
    allocatedBlockGroups.clear();
    allocatedBlocks.clear();
    allocatedStructs.clear();
    allocatedClasses.clear();
    allocatedEnums.clear();
}

// FileName API
void CHIRContext::RegisterSourceFileName(unsigned fileId, const std::string& fileName) const
{
    // we need to insert or assign, because this `fileNameMap` may be set in deserialization when
    // we are compiling specific package, so this old `fileNameMap` is from common package,
    // it's not guaranteed that common package's file order and size are same with specific's
    fileNameMap->insert_or_assign(fileId, fileName);
}

const std::string& CHIRContext::GetSourceFileName(unsigned fileId) const
{
    if (auto it = this->fileNameMap->find(fileId); it != this->fileNameMap->end()) {
        return it->second;
    }
    return INVALID_NAME;
}

const std::unordered_map<unsigned int, std::string>* CHIRContext::GetFileNameMap() const
{
    return this->fileNameMap;
}

StructType* CHIRContext::GetStructType(const std::string& package, const std::string& name,
    const std::vector<std::string>& genericType, ModalInfo modal) const
{
    (void)modal;
    std::vector<StructDef*> structs = this->curPackage->GetStructs();
    std::vector<StructDef*> importStructs = this->curPackage->GetImportedStructs();
    structs.insert(structs.end(), importStructs.cbegin(), importStructs.cend());
    for (auto it : structs) {
        if (it->GetPackageName() != package || it->GetSrcCodeIdentifier() != name) {
            continue;
        }
        if (it->TestAttr(Attribute::GENERIC_INSTANTIATED)) {
            continue;
        }
        auto structType = StaticCast<StructType*>(it->GetType());
        auto argTypes = structType->GetGenericArgs();
        if (std::equal(genericType.begin(), genericType.end(), argTypes.begin(),
                       [](const std::string& a, const Type* b) { return a == b->ToString(); })) {
            return structType;
        }
    }
    return nullptr;
}

void CHIRContext::MergeTypes()
{
    this->constAllocatedTys.merge(this->dynamicAllocatedTys);
}

StructType* CHIRContext::GetStringTy(ModalInfo modal) const
{
    return GetStructType("std.core", "String", {}, modal);
}

Type* CHIRContext::ToSelectorType(Type::TypeKind kind) const
{
    switch (kind) {
        case Type::TypeKind::TYPE_UINT32:
            return GetUInt32Ty();
        default:
            return GetBoolTy();
    }
}

Type* CHIRContext::SubstituteModal(Type* ty, ModalInfo modal)
{
    if (ty->Modal() == modal) {
        return ty;
    }
    // const_cast is safe here: GetType only modifies internal type caches
    auto* ctx = const_cast<CHIRContext*>(this);
    switch (ty->GetTypeKind()) {
        case Type::TypeKind::TYPE_TUPLE: {
            auto* tupleTy = StaticCast<TupleType*>(ty);
            return ctx->GetType<TupleType>(tupleTy->GetElementTypes(), modal);
        }
        case Type::TypeKind::TYPE_STRUCT: {
            auto* structTy = StaticCast<StructType*>(ty);
            return ctx->GetType<StructType>(structTy->GetStructDef(), structTy->GetGenericArgs(), modal);
        }
        case Type::TypeKind::TYPE_ENUM: {
            auto* enumTy = StaticCast<EnumType*>(ty);
            return ctx->GetType<EnumType>(enumTy->GetEnumDef(), enumTy->GetGenericArgs(), modal);
        }
        case Type::TypeKind::TYPE_FUNC: {
            auto* funcTy = StaticCast<FuncType*>(ty);
            return ctx->GetType<FuncType>(
                funcTy->GetParamTypes(), funcTy->GetReturnType(), funcTy->HasVarArg(), ty->IsCFunc(), modal);
        }
        case Type::TypeKind::TYPE_CLASS: {
            auto* classTy = StaticCast<ClassType*>(ty);
            return ctx->GetType<ClassType>(classTy->GetClassDef(), classTy->GetGenericArgs(), modal);
        }
        case Type::TypeKind::TYPE_RAWARRAY: {
            auto* rawArrayTy = StaticCast<RawArrayType*>(ty);
            return ctx->GetType<RawArrayType>(rawArrayTy->GetElementType(), rawArrayTy->GetDims(), modal);
        }
        case Type::TypeKind::TYPE_VARRAY: {
            auto* varrayTy = StaticCast<VArrayType*>(ty);
            return ctx->GetType<VArrayType>(
                varrayTy->GetElementType(), static_cast<int64_t>(varrayTy->GetSize()), modal);
        }
        case Type::TypeKind::TYPE_CPOINTER: {
            auto* cptrTy = StaticCast<CPointerType*>(ty);
            return ctx->GetType<CPointerType>(cptrTy->GetElementType(), modal);
        }
        case Type::TypeKind::TYPE_CSTRING: {
            return ctx->GetType<CStringType>(modal);
        }
        case Type::TypeKind::TYPE_GENERIC: {
            auto* genericTy = StaticCast<GenericType*>(ty);
            return ctx->GetType<GenericType>(genericTy->GetIdentifier(), genericTy->GetSrcCodeIdentifier(), modal);
        }
        case Type::TypeKind::TYPE_REFTYPE: {
            // substitute modal of underlying type
            auto ref = StaticCast<RefType*>(ty);
            return GetType<RefType>(SubstituteModal(ref->GetBaseType(), modal));
        }
        default:
            return ty;
    }
}
