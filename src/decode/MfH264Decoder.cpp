#include "decode/MfH264Decoder.h"

#include "common/Log.h"

namespace remote::decode {

Result MfH264Decoder::Initialize() {
    auto result = mf_.Initialize();
    if (!result) {
        return result;
    }
    // TODO: Create and configure an H.264 decoder MFT that outputs D3D11-compatible frames.
    Log(LogLevel::Info, "MfH264Decoder initialized (stub)");
    return Result::Ok();
}

Result MfH264Decoder::DecodeStub(const ByteBuffer& encoded) {
    // TODO: Feed encoded H.264 access units into the decoder MFT and produce D3D11 textures.
    Logf(LogLevel::Debug, "MfH264Decoder decode stub, {} bytes", encoded.size());
    return Result::Ok();
}

} // namespace remote::decode

