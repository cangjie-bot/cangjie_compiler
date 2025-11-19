// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_MARK_CLASS_HASINITED_H
#define CANGJIE_CHIR_TRANSFORMATION_MARK_CLASS_HASINITED_H

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Package.h"

namespace Cangjie::CHIR {
/**
 * CHIR normal Pass: add has invited flag to class which has finalizer, in case of finalize before init.
 */
class MarkClassHasInited {
public:
    explicit MarkClassHasInited(CHIRBuilder& builder);

    void RunOnPackage(const Package& package);

private:
    void AddHasInitedFlagToClassDef(ClassDef& classDef);
    void AddGuardToFinalizer(ClassDef& classDef);
    void AssignHasInitedFlagToFalseInConstructorHead(Func& constructor);
    void AssignHasInitedFlagToTrueInConstructorExit(Func& constructor);

private:
    CHIRBuilder& builder;
};
} // namespace Cangjie::CHIR

#endif