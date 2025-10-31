// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares core support for java mirror and mirror subtype
 */
#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_DESUGAR_MEMBER_MAP_CACHE
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_DESUGAR_MEMBER_MAP_CACHE

#include "cangjie/AST/Node.h"
#include "InheritanceChecker/MemberSignature.h"

using namespace Cangjie::AST;

namespace Cangjie {
    struct MemberSignature;
}

namespace Cangjie::Interop::Java {

struct MemberMapCache {
    const std::unordered_map<Ptr<const InheritableDecl>, std::multimap<std::string, MemberSignature>> interfaceMembers;
    const std::unordered_map<Ptr<const InheritableDecl>, std::multimap<std::string, MemberSignature>> instanceMembers;

    MemberMapCache(
        std::unordered_map<Ptr<const InheritableDecl>, std::multimap<std::string, MemberSignature>> interfaceMembers,
        std::unordered_map<Ptr<const InheritableDecl>, std::multimap<std::string, MemberSignature>> instanceMembers)
        : interfaceMembers(std::move(interfaceMembers)), instanceMembers(std::move(instanceMembers)) {}
};

}

#endif