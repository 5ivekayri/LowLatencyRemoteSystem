#include "encode/MfH264Encoder.h"

#include "common/Log.h"

#include <mfapi.h>

namespace remote::encode {

MfStartup::~MfStartup() {
    if (initialized_) {
        MFShutdown();
    }
}

Result MfStartup::Initialize() {
    if (initialized_) {
        return Result::Ok();
    }

    const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        LogHResult("MFStartup", hr);
        return Result::Fail("Media Foundation startup failed");
    }
    initialized_ = true;
    Log(LogLevel::Info, "Media Foundation initialized");
    return Result::Ok();
}

Result MfH264Encoder::Initialize(const VideoEncoderConfig& config) {
    auto mf = mf_.Initialize();
    if (!mf) {
        return mf;
    }
    config_ = config;

    // TODO: Create an H.264 encoder MFT and configure low latency, no B-frames, bitrate, FPS, and GOP.
    Logf(LogLevel::Info,
         "MfH264Encoder initialized (stub): {}x{}@{} {}bps lowLatency={} allowBFrames={}",
         config_.width,
         config_.height,
         config_.fps,
         config_.bitrateBps,
         config_.lowLatency,
         config_.allowBFrames);
    return Result::Ok();
}

Result MfH264Encoder::RequestKeyFrame() {
    // TODO: Signal the encoder MFT for an IDR/keyframe once transform is implemented.
    Log(LogLevel::Info, "MfH264Encoder keyframe requested (stub)");
    return Result::Ok();
}

} // namespace remote::encode

