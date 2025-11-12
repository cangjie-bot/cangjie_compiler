// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares type aliases for inheritance checking of structure declarations.
 */

#ifndef CANGJIE_SEMA_INHERITANCE_CHECKER_TYPE_ALIASES_H
#define CANGJIE_SEMA_INHERITANCE_CHECKER_TYPE_ALIASES_H

#include "MemberSignature.h"

namespace Cangjie {
using MemberMap = std::multimap<std::string, MemberSignature>;
}
#endif
