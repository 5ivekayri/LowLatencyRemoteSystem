#pragma once

#include <chrono>
#include <cstdint>

namespace remote {

using SteadyClock = std::chrono::steady_clock;

inline uint64_t NowUs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            SteadyClock::now().time_since_epoch())
            .count());
}

struct FrameTelemetry {
    uint64_t captureStartUs = 0;
    uint64_t captureDoneUs = 0;
    uint64_t encodeStartUs = 0;
    uint64_t encodeDoneUs = 0;
    uint64_t packetSendUs = 0;
    uint64_t clientReceiveUs = 0;
    uint64_t reassembledUs = 0;
    uint64_t decodeDoneUs = 0;
    uint64_t presentDoneUs = 0;
};

} // namespace remote

