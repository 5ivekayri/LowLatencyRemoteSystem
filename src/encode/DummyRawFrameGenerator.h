#pragma once

#include "common/ByteBuffer.h"
#include "common/Clock.h"
#include "protocol/Protocol.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace remote::encode {

struct DummyRawFrame {
    protocol::VideoFrameHeader header;
    ByteBuffer bytes;
};

class DummyRawFrameGenerator {
public:
    DummyRawFrameGenerator(uint32_t width, uint32_t height)
        : width_(width), height_(height), strideBytes_(width * 4) {}

    DummyRawFrame Generate(uint64_t frameId) const {
        DummyRawFrame frame;
        frame.header.payloadFormat = static_cast<uint32_t>(protocol::VideoPayloadFormat::RawBGRA8);
        frame.header.width = width_;
        frame.header.height = height_;
        frame.header.strideBytes = strideBytes_;
        frame.header.frameId = frameId;
        frame.header.captureTimestampUs = NowUs();
        frame.header.payloadSizeBytes = strideBytes_ * height_;
        frame.bytes.resize(frame.header.payloadSizeBytes);

        const uint32_t phase = static_cast<uint32_t>(frameId * 3);
        for (uint32_t y = 0; y < height_; ++y) {
            auto* row = reinterpret_cast<uint8_t*>(frame.bytes.data() + static_cast<size_t>(y) * strideBytes_);
            for (uint32_t x = 0; x < width_; ++x) {
                const uint8_t blue = static_cast<uint8_t>((x + phase) & 0xFF);
                const uint8_t green = static_cast<uint8_t>((y * 2 + phase) & 0xFF);
                const uint8_t red = static_cast<uint8_t>(((x / 8) + (y / 8) + phase) & 0xFF);
                const size_t offset = static_cast<size_t>(x) * 4;
                row[offset + 0] = blue;
                row[offset + 1] = green;
                row[offset + 2] = red;
                row[offset + 3] = 0xFF;
            }
        }

        DrawColorBars(frame);
        DrawMovingSquare(frame, frameId);
        DrawFrameCounterRegion(frame, frameId);
        return frame;
    }

private:
    void FillRect(DummyRawFrame& frame, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint8_t b, uint8_t g, uint8_t r) const {
        const uint32_t x1 = std::min(width_, x0 + w);
        const uint32_t y1 = std::min(height_, y0 + h);
        for (uint32_t y = y0; y < y1; ++y) {
            auto* row = reinterpret_cast<uint8_t*>(frame.bytes.data() + static_cast<size_t>(y) * strideBytes_);
            for (uint32_t x = x0; x < x1; ++x) {
                const size_t offset = static_cast<size_t>(x) * 4;
                row[offset + 0] = b;
                row[offset + 1] = g;
                row[offset + 2] = r;
                row[offset + 3] = 0xFF;
            }
        }
    }

    void DrawColorBars(DummyRawFrame& frame) const {
        constexpr uint8_t bars[][3] = {
            {255, 255, 255}, {0, 255, 255}, {255, 255, 0}, {0, 255, 0},
            {255, 0, 255},   {0, 0, 255},   {255, 0, 0},   {0, 0, 0},
        };
        const uint32_t barWidth = std::max(1u, width_ / static_cast<uint32_t>(std::size(bars)));
        for (uint32_t i = 0; i < static_cast<uint32_t>(std::size(bars)); ++i) {
            FillRect(frame, i * barWidth, 0, barWidth, 48, bars[i][0], bars[i][1], bars[i][2]);
        }
    }

    void DrawMovingSquare(DummyRawFrame& frame, uint64_t frameId) const {
        constexpr uint32_t squareSize = 96;
        const uint32_t spanX = (width_ > squareSize) ? width_ - squareSize : 1;
        const uint32_t spanY = (height_ > squareSize + 64) ? height_ - squareSize - 64 : 1;
        const uint32_t x = static_cast<uint32_t>((frameId * 7) % spanX);
        const uint32_t y = 64 + static_cast<uint32_t>((frameId * 5) % spanY);
        FillRect(frame, x, y, squareSize, squareSize, 32, 220, 255);
        FillRect(frame, x + 10, y + 10, squareSize - 20, squareSize - 20, 16, 32, 48);
    }

    void DrawFrameCounterRegion(DummyRawFrame& frame, uint64_t frameId) const {
        FillRect(frame, 24, height_ > 88 ? height_ - 88 : 0, 360, 64, 20, 20, 20);
        uint64_t value = frameId;
        for (uint32_t digit = 0; digit < 12; ++digit) {
            const bool on = (value & 1u) != 0;
            FillRect(frame, 40 + digit * 26, height_ > 72 ? height_ - 72 : 0, 18, 40, on ? 0 : 60, on ? 255 : 60, on ? 120 : 60);
            value >>= 1u;
        }
    }

    uint32_t width_ = 1280;
    uint32_t height_ = 720;
    uint32_t strideBytes_ = 1280 * 4;
};

} // namespace remote::encode
