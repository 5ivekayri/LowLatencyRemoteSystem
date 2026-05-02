#pragma once

#include "common/ByteBuffer.h"
#include "common/Result.h"

#include <cstdint>

namespace remote::encode {

struct VideoEncoderConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 60;
    uint32_t bitrateBps = 25'000'000;
    uint32_t gopSeconds = 1;
    bool lowLatency = true;
    bool allowBFrames = false;
};

struct EncodedFrame {
    uint64_t frameId = 0;
    uint64_t timestampUs = 0;
    bool keyFrame = false;
    ByteBuffer bytes;
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual Result Initialize(const VideoEncoderConfig& config) = 0;
    virtual Result RequestKeyFrame() = 0;
};

} // namespace remote::encode

