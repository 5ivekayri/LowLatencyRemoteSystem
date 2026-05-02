#pragma once

#include <cstdint>

namespace remote::net {

class RateController {
public:
    [[nodiscard]] uint32_t targetBitrateBps() const noexcept { return targetBitrateBps_; }
    void setTargetBitrateBps(uint32_t bitrate) noexcept { targetBitrateBps_ = bitrate; }

private:
    uint32_t targetBitrateBps_ = 25'000'000;
};

} // namespace remote::net

