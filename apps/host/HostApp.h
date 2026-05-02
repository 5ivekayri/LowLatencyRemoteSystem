#pragma once

#include "capture/DxgiDuplicator.h"
#include "common/Result.h"
#include "encode/MfH264Encoder.h"
#include "input/InputInjector.h"
#include "protocol/Protocol.h"
#include "transport/TcpControlChannel.h"
#include "transport/UdpVideoTransport.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <span>

namespace remote::host {

enum class CaptureMode {
    Dummy,
    Dxgi,
};

struct HostConfig {
    std::string token = "dev-token";
    std::string bindAddress = "127.0.0.1";
    uint16_t tcpPort = 48000;
    uint16_t udpPort = 47991;
    CaptureMode mode = CaptureMode::Dummy;
    bool listAdapters = false;
    std::string selfTest;
    std::string udpTarget = "127.0.0.1";
    uint32_t adapterIndex = 0;
    uint32_t outputIndex = 0;
    uint32_t fps = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

class HostApp {
public:
    explicit HostApp(HostConfig config);

    Result Initialize();
    int Run();

private:
    Result InitializeFrameSource();
    Result RunHandshake();
    Result RunRawVideoLoop();
    Result SendRawFrame(const protocol::VideoFrameHeader& frameHeader, std::span<const std::byte> payload, uint64_t& sequence);

    HostConfig config_;
    std::string clientUdpAddress_ = "127.0.0.1";
    uint16_t clientUdpPort_ = 48001;
    uint64_t sessionId_ = 0x4C4C52534D565030ULL; // LLRS_MVP0
    uint32_t streamWidth_ = 640;
    uint32_t streamHeight_ = 360;
    bool frameSourceInitialized_ = false;
    transport::TcpControlChannel control_;
    transport::UdpVideoTransport video_;
    capture::DxgiDuplicator capture_;
    encode::MfH264Encoder encoder_;
    input::InputInjector input_;
};

} // namespace remote::host
