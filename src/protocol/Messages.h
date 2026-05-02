#pragma once

#include <cstdint>
#include <string>

namespace remote::protocol {

struct Hello {
    uint16_t protocolVersion = Version;
    std::string clientName;
    std::string token;
    uint16_t udpPort = 47991;
};

struct HostInfo {
    std::string hostName;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t refreshHz = 60;
};

struct StartStream {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 60;
    uint32_t bitrateBps = 25'000'000;
};

struct Stats {
    uint32_t rttMs = 0;
    uint32_t packetLossPermille = 0;
    uint32_t encodeQueueDepth = 0;
    uint32_t decodeQueueDepth = 0;
};

} // namespace remote::protocol
