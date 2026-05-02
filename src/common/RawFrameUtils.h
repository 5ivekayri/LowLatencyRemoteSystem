#pragma once

#include "common/ByteBuffer.h"
#include "common/Result.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace remote {

struct RawFrameValidation {
    bool valid = false;
    uint64_t checksum = 0;
    size_t uniqueSampledColors = 0;
    std::string message;
};

inline RawFrameValidation ValidateRawBGRAFrame(uint32_t width, uint32_t height, uint32_t strideBytes, std::span<const std::byte> data) {
    RawFrameValidation result;
    if (width == 0 || height == 0) {
        result.message = "width/height must be non-zero";
        return result;
    }
    if (strideBytes < width * 4) {
        result.message = "strideBytes is smaller than width*4";
        return result;
    }
    const size_t required = static_cast<size_t>(strideBytes) * height;
    if (data.size() < required) {
        result.message = "data is smaller than strideBytes*height";
        return result;
    }

    const uint32_t stepX = std::max(1u, width / 64);
    const uint32_t stepY = std::max(1u, height / 64);
    bool allWhite = true;
    bool allBlack = true;
    std::unordered_set<uint32_t> colors;

    for (uint32_t y = 0; y < height; y += stepY) {
        const auto* row = data.data() + static_cast<size_t>(y) * strideBytes;
        for (uint32_t x = 0; x < width; x += stepX) {
            const auto* px = row + static_cast<size_t>(x) * 4;
            const uint8_t b = static_cast<uint8_t>(px[0]);
            const uint8_t g = static_cast<uint8_t>(px[1]);
            const uint8_t r = static_cast<uint8_t>(px[2]);
            const uint32_t color = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
            result.checksum = (result.checksum * 1315423911ull) ^ color;
            colors.insert(color);
            allWhite = allWhite && r == 255 && g == 255 && b == 255;
            allBlack = allBlack && r == 0 && g == 0 && b == 0;
        }
    }

    result.uniqueSampledColors = colors.size();
    if (result.checksum == 0) {
        result.message = "sampled checksum is zero";
        return result;
    }
    if (allWhite) {
        result.message = "all sampled pixels are white";
        return result;
    }
    if (allBlack) {
        result.message = "all sampled pixels are black";
        return result;
    }
    if (colors.size() < 16) {
        result.message = "fewer than 16 unique sampled colors";
        return result;
    }

    result.valid = true;
    result.message = "ok";
    return result;
}

inline ByteBuffer ResizeBGRA8Nearest(std::span<const std::byte> srcData, uint32_t srcW, uint32_t srcH, uint32_t srcStride, uint32_t dstW, uint32_t dstH) {
    ByteBuffer out;
    if (srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0 || srcStride < srcW * 4 ||
        srcData.size() < static_cast<size_t>(srcStride) * srcH) {
        return out;
    }

    const uint32_t dstStride = dstW * 4;
    out.resize(static_cast<size_t>(dstStride) * dstH);
    for (uint32_t y = 0; y < dstH; ++y) {
        const uint32_t srcY = static_cast<uint32_t>((static_cast<uint64_t>(y) * srcH) / dstH);
        const auto* srcRow = srcData.data() + static_cast<size_t>(srcY) * srcStride;
        auto* dstRow = out.data() + static_cast<size_t>(y) * dstStride;
        for (uint32_t x = 0; x < dstW; ++x) {
            const uint32_t srcX = static_cast<uint32_t>((static_cast<uint64_t>(x) * srcW) / dstW);
            const auto* srcPx = srcRow + static_cast<size_t>(srcX) * 4;
            auto* dstPx = dstRow + static_cast<size_t>(x) * 4;
            dstPx[0] = srcPx[0];
            dstPx[1] = srcPx[1];
            dstPx[2] = srcPx[2];
            dstPx[3] = srcPx[3];
        }
    }
    return out;
}

inline Result SaveBGRA8ToBMP(const std::filesystem::path& path, uint32_t width, uint32_t height, uint32_t strideBytes, std::span<const std::byte> data) {
    if (width == 0 || height == 0 || strideBytes < width * 4 || data.size() < static_cast<size_t>(strideBytes) * height) {
        return Result::Fail("invalid BGRA frame for BMP save");
    }

    std::filesystem::create_directories(path.parent_path());

#pragma pack(push, 1)
    struct BmpFileHeader {
        uint16_t type = 0x4D42;
        uint32_t size = 0;
        uint16_t reserved1 = 0;
        uint16_t reserved2 = 0;
        uint32_t offBits = 54;
    };
    struct BmpInfoHeader {
        uint32_t size = 40;
        int32_t width = 0;
        int32_t height = 0;
        uint16_t planes = 1;
        uint16_t bitCount = 32;
        uint32_t compression = 0;
        uint32_t sizeImage = 0;
        int32_t xPelsPerMeter = 0;
        int32_t yPelsPerMeter = 0;
        uint32_t clrUsed = 0;
        uint32_t clrImportant = 0;
    };
#pragma pack(pop)

    BmpFileHeader file;
    BmpInfoHeader info;
    info.width = static_cast<int32_t>(width);
    info.height = -static_cast<int32_t>(height);
    info.sizeImage = width * height * 4;
    file.size = file.offBits + info.sizeImage;

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return Result::Fail("open BMP output failed");
    }
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    for (uint32_t y = 0; y < height; ++y) {
        const auto* row = data.data() + static_cast<size_t>(y) * strideBytes;
        out.write(reinterpret_cast<const char*>(row), static_cast<std::streamsize>(width * 4));
    }
    return out ? Result::Ok() : Result::Fail("write BMP output failed");
}

} // namespace remote
