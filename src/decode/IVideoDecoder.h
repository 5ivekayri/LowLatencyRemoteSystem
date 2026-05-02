#pragma once

#include "common/ByteBuffer.h"
#include "common/Result.h"

namespace remote::decode {

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;
    virtual Result Initialize() = 0;
    virtual Result DecodeStub(const ByteBuffer& encoded) = 0;
};

} // namespace remote::decode

