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
#include "cangjie/CHIR/Utils/Utils.h"

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
    Init();
}

void CHIRContext::Init()
{
    unitTy = GetType<UnitType>();
    boolTy = GetType<BooleanType>();
    runeTy = GetType<RuneType>();
    nothingTy = GetType<NothingType>();
    int8Ty = GetType<IntType>(Type::TypeKind::TYPE_INT8);
    int16Ty = GetType<IntType>(Type::TypeKind::TYPE_INT16);
    int32Ty = GetType<IntType>(Type::TypeKind::TYPE_INT32);
    int64Ty = GetType<IntType>(Type::TypeKind::TYPE_INT64);
    intNativeTy = GetType<IntType>(Type::TypeKind::TYPE_INT_NATIVE);
    uint8Ty = GetType<IntType>(Type::TypeKind::TYPE_UINT8);
    uint16Ty = GetType<IntType>(Type::TypeKind::TYPE_UINT16);
    uint32Ty = GetType<IntType>(Type::TypeKind::TYPE_UINT32);
    uint64Ty = GetType<IntType>(Type::TypeKind::TYPE_UINT64);
    uintNativeTy = GetType<IntType>(Type::TypeKind::TYPE_UINT_NATIVE);
    float16Ty = GetType<FloatType>(Type::TypeKind::TYPE_FLOAT16);
    float32Ty = GetType<FloatType>(Type::TypeKind::TYPE_FLOAT32);
    float64Ty = GetType<FloatType>(Type::TypeKind::TYPE_FLOAT64);
    cstringTy = GetType<CStringType>();
    voidTy = GetType<VoidType>();
}

void CHIRContext::DeleteAll()
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

CHIRContext::~CHIRContext()
{
    DeleteAll();
}

// FileName API
void CHIRContext::RegisterSourceFileName(unsigned fileId, const std::string& fileName) const
{
    // we need to insert or assign, because this `fileNameMap` may be set in deserialization when
    // we are compiling platform package, so this old `fileNameMap` is from common package,
    // it's not guaranteed that common package's file order and size are same with platform's
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

StructType* CHIRContext::GetStructType(
    const std::string& package, const std::string& name, const std::vector<std::string>& genericType) const
{
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

StructType* CHIRContext::GetStringTy() const
{
    return GetStructType("std.core", "String");
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

ClassType* CHIRContext::SearchObjectTyInPackage() const
{
    for (auto classDef : this->curPackage->GetImportedClasses()) {
        if (IsCoreObject(*classDef)) {
            return classDef->GetType();
        }
    }
    for (auto classDef : this->curPackage->GetClasses()) {
        if (IsCoreObject(*classDef)) {
            return classDef->GetType();
        }
    }
    return nullptr;
}

ClassType* CHIRContext::SearchAnyTyInPackage() const
{
    for (auto classDef : this->curPackage->GetImportedClasses()) {
        if (IsCoreAny(*classDef)) {
            return classDef->GetType();
        }
    }
    for (auto classDef : this->curPackage->GetClasses()) {
        if (IsCoreAny(*classDef)) {
            return classDef->GetType();
        }
    }
    return nullptr;
}

void CHIRContext::SwapContext(CHIRContext& other)
{
    auto temp1 = other.curPackage;
    other.curPackage = curPackage;
    curPackage = temp1;
    auto temp2 = other.fileNameMap;
    other.fileNameMap = fileNameMap;
    fileNameMap = temp2;
    auto temp3 = other.allocatedExprs;
    other.allocatedExprs = allocatedExprs;
    allocatedExprs = temp3;
    auto temp4 = other.allocatedValues;
    other.allocatedValues = allocatedValues;
    allocatedValues = temp4;
    auto temp5 = other.allocatedBlockGroups;
    other.allocatedBlockGroups = allocatedBlockGroups;
    allocatedBlockGroups = temp5;
    auto temp6 = other.allocatedBlocks;
    other.allocatedBlocks = allocatedBlocks;
    allocatedBlocks = temp6;
    auto temp7 = other.allocatedStructs;
    other.allocatedStructs = allocatedStructs;
    allocatedStructs = temp7;
    auto temp8 = other.allocatedClasses;
    other.allocatedClasses = allocatedClasses;
    allocatedClasses = temp8;
    auto temp9 = other.allocatedEnums;
    other.allocatedEnums = allocatedEnums;
    allocatedEnums = temp9;
    auto temp10 = other.allocatedExtends;
    other.allocatedExtends = allocatedExtends;
    allocatedExtends = temp10;
    auto temp11 = other.threadsNum;
    other.threadsNum = threadsNum;
    threadsNum = temp11;
    auto temp12 = other.dynamicAllocatedTys;
    other.dynamicAllocatedTys = dynamicAllocatedTys;
    dynamicAllocatedTys = temp12;
    auto temp13 = other.constAllocatedTys;
    other.constAllocatedTys = constAllocatedTys;
    constAllocatedTys = temp13;
    auto temp14 = other.unitTy;
    other.unitTy = unitTy;
    unitTy = temp14;
    auto temp15 = other.boolTy;
    other.boolTy = boolTy;
    boolTy = temp15;
    auto temp16 = other.runeTy;
    other.runeTy = runeTy;
    runeTy = temp16;
    auto temp17 = other.nothingTy;
    other.nothingTy = nothingTy;
    nothingTy = temp17;
    auto temp18 = other.int8Ty;
    other.int8Ty = int8Ty;
    int8Ty = temp18;
    auto temp19 = other.int16Ty;
    other.int16Ty = int16Ty;
    int16Ty = temp19;
    auto temp20 = other.int32Ty;
    other.int32Ty = int32Ty;
    int32Ty = temp20;
    auto temp21 = other.int64Ty;
    other.int64Ty = int64Ty;
    int64Ty = temp21;
    auto temp22 = other.intNativeTy;
    other.intNativeTy = intNativeTy;
    intNativeTy = temp22;
    auto temp23 = other.uint8Ty;
    other.uint8Ty = uint8Ty;
    uint8Ty = temp23;
    auto temp24 = other.uint16Ty;
    other.uint16Ty = uint16Ty;
    uint16Ty = temp24;
    auto temp25 = other.uint32Ty;
    other.uint32Ty = uint32Ty;
    uint32Ty = temp25;
    auto temp26 = other.uint64Ty;
    other.uint64Ty = uint64Ty;
    uint64Ty = temp26;
    auto temp27 = other.uintNativeTy;
    other.uintNativeTy = uintNativeTy;
    uintNativeTy = temp27;
    auto temp28 = other.float16Ty;
    other.float16Ty = float16Ty;
    float16Ty = temp28;
    auto temp29 = other.float32Ty;
    other.float32Ty = float32Ty;
    float32Ty = temp29;
    auto temp30 = other.float64Ty;
    other.float64Ty = float64Ty;
    float64Ty = temp30;
    auto temp31 = other.cstringTy;  
    other.cstringTy = cstringTy;
    cstringTy = temp31;
    auto temp32 = other.objectTy;
    other.objectTy = objectTy;
    objectTy = temp32;
    auto temp33 = other.anyTy;
    other.anyTy = anyTy;
    anyTy = temp33;
    auto temp34 = other.voidTy;
    other.voidTy = voidTy;
    voidTy = temp34;
}