// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_SERIALIZER_CHIRSERIALIZER_H
#define CANGJIE_CHIR_SERIALIZER_CHIRSERIALIZER_H

#include "cangjie/CHIR/CHIR.h"
#include "cangjie/CHIR/Package.h"

#ifdef CANGJIE_CHIR_PLUGIN
namespace flatbuffers {
    class DetachedBuffer;
}
#endif
namespace Cangjie::CHIR {
#ifdef CANGJIE_CHIR_PLUGIN
struct DetachedBuffer {
    DetachedBuffer(class ::flatbuffers::DetachedBuffer&& buf);
    ~DetachedBuffer();

    void* Data();
    size_t Size();
private:
    struct DetachedBufferImpl* impl;
};
#endif

class CHIRSerializer {
    class CHIRSerializerImpl;
public:
    static void Serialize(const Package& package, const std::string filename, ToCHIR::Phase phase);
#ifdef CANGJIE_CHIR_PLUGIN
    static DetachedBuffer Serialize(const Package& package, ToCHIR::Phase phase);
#endif
};
} // namespace Cangjie::CHIR
#endif
