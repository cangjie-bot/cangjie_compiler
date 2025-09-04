// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGCLOSURETYPE_H
#define CANGJIE_CGCLOSURETYPE_H

#include "Base/CGTypes/CGType.h"

namespace Cangjie {
namespace CodeGen {

class CGClosureType : public CGType {
    friend class CGTypeMgr;

public:
    static std::string GetTypeNameByClosureType(const CHIR::ClosureType& closureType);

protected:
    llvm::Type* GenLLVMType() override;
    void GenContainedCGTypes() override;

private:
    CGClosureType() = delete;

    explicit CGClosureType(CGModule& cgMod, CGContext& cgCtx, const CHIR::Type& chirType)
        : CGType(cgMod, cgCtx, chirType)
    {
    }

    llvm::Constant* GenSourceGenericOfTypeInfo() override;
    llvm::Constant* GenTypeArgsNumOfTypeInfo() override;
    llvm::Constant* GenTypeArgsOfTypeInfo() override;

    void CalculateSizeAndAlign() override;
};
} // namespace CodeGen
} // namespace Cangjie

#endif // CANGJIE_CGCLOSURETYPE_H
