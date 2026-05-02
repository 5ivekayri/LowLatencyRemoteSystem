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
#include <chrono>
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
    uint32_t maxMbps = 150;
    bool maxMbpsProvided = false;
    uint32_t mtuPayloadBytes = 1150;
    bool packetPacingEnabled = true;
    uint32_t udpSendBufferBytes = 8 * 1024 * 1024;
};

struct RawStreamConfig {
    uint32_t targetWidth = 640;
    uint32_t targetHeight = 360;
    uint32_t targetFps = 10;
    uint32_t maxMbps = 150;
    uint32_t mtuPayloadBytes = 1150;
    bool packetPacingEnabled = true;
    uint32_t udpSendBufferBytes = 8 * 1024 * 1024;
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
    struct SendFrameResult {
        uint64_t packetsSent = 0;
        uint64_t bytesSent = 0;
        bool dropped = false;
        int lastWin32Error = 0;
    };
    ResultT<SendFrameResult> SendRawFrame(const protocol::VideoFrameHeader& frameHeader, std::span<const std::byte> payload, uint64_t& sequence);
    Result ValidateRawStreamBudget() const;

    HostConfig config_;
    RawStreamConfig rawStream_;
    std::string clientUdpAddress_ = "127.0.0.1";
    uint16_t clientUdpPort_ = 48001;
    uint64_t sessionId_ = 0x4C4C52534D565030ULL; // LLRS_MVP0
    uint32_t streamWidth_ = 640;
    uint32_t streamHeight_ = 360;
    uint32_t sourceWidth_ = 640;
    uint32_t sourceHeight_ = 360;
    bool frameSourceInitialized_ = false;
    double pacingTokens_ = 0.0;
    std::chrono::steady_clock::time_point pacingLast_{};
    transport::TcpControlChannel control_;
    transport::UdpVideoTransport video_;
    capture::DxgiDuplicator capture_;
    encode::MfH264Encoder encoder_;
    input::InputInjector input_;
};

} // namespace remote::host
