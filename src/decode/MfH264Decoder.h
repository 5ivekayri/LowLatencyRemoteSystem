#pragma once

#include "decode/IVideoDecoder.h"
#include "encode/MfH264Encoder.h"

namespace remote::decode {

class MfH264Decoder final : public IVideoDecoder {
public:
    Result Initialize() override;
    Result DecodeStub(const ByteBuffer& encoded) override;

private:
    encode::MfStartup mf_;
};

} // namespace remote::decode

