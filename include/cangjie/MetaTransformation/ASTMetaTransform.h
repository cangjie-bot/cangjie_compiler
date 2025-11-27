// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_METATRANSFORM_ASTMETATRANSFORM_H
#define CANGJIE_METATRANSFORM_ASTMETATRANSFORM_H
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND

namespace Cangjie {
enum class ASTMetaTransformStage {
    PARSE,
    SEMA,
};
} // namespace Cangjie
#endif // CANGJIE_CODEGEN_CJNATIVE_BACKEND
#endif // CANGJIE_METATRANSFORM_ASTMETATRANSFORM_H
