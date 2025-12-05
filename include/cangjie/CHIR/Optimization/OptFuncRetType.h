// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_OPT_FUNC_RET_TYPE_H
#define CANGJIE_CHIR_OPT_FUNC_RET_TYPE_H

#include "cangjie/CHIR/IR/CHIRBuilder.h"

namespace Cangjie::CHIR {

class OptFuncRetType {
public:
    explicit OptFuncRetType(Package& package, CHIRBuilder& builder);
    void Unit2Void();

private:
    Package& package;
    CHIRBuilder& builder;
};

} // namespace Cangjie::CHIR

#endif