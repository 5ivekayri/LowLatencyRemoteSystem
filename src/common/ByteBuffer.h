#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace remote {

using ByteBuffer = std::vector<std::byte>;

inline std::span<const std::byte> AsBytes(const ByteBuffer& buffer) noexcept {
    return {buffer.data(), buffer.size()};
}

inline std::span<std::byte> AsWritableBytes(ByteBuffer& buffer) noexcept {
    return {buffer.data(), buffer.size()};
}

} // namespace remote

