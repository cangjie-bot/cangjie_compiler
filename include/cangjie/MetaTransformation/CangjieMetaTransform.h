// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_METATRANSFORMATION_CANGJIEMETATRANSFORM_H
#define CANGJIE_METATRANSFORMATION_CANGJIEMETATRANSFORM_H
#include <cstdint>

namespace Cangjie {
extern "C" struct Memory {
    void* ptr;
    uint64_t size;
};
}
#endif // CANGJIE_METATRANSFORMATION_CANGJIEMETATRANSFORM_H