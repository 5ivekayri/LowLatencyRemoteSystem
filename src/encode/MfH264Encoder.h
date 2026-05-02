#pragma once

#include "encode/IVideoEncoder.h"

namespace remote::encode {

class MfStartup {
public:
    MfStartup() = default;
    ~MfStartup();

    MfStartup(const MfStartup&) = delete;
    MfStartup& operator=(const MfStartup&) = delete;

    Result Initialize();

private:
    bool initialized_ = false;
};

class MfH264Encoder final : public IVideoEncoder {
public:
    Result Initialize(const VideoEncoderConfig& config) override;
    Result RequestKeyFrame() override;

private:
    MfStartup mf_;
    VideoEncoderConfig config_;
};

} // namespace remote::encode

