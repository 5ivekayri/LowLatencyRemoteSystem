#pragma once

#include "common/Result.h"
#include "decode/MfH264Decoder.h"
#include "input/ClientInputCapture.h"
#include "render/D3D11Renderer.h"
#include "transport/TcpControlChannel.h"
#include "transport/UdpVideoTransport.h"

#include <cstdint>
#include <string>

namespace remote::client {

struct ClientConfig {
    std::string host = "127.0.0.1";
    std::string bindAddress = "0.0.0.0";
    std::string token = "dev-token";
    std::string selfTest;
    uint16_t tcpPort = 48000;
    uint16_t udpPort = 48001;
};

class ClientApp {
public:
    explicit ClientApp(ClientConfig config);

    Result Initialize();
    int Run();

private:
    Result RunHandshake();
    Result RunDummyVideoReceiveLoop();

    ClientConfig config_;
    uint32_t streamWidth_ = 640;
    uint32_t streamHeight_ = 360;
    transport::TcpControlChannel control_;
    transport::UdpVideoTransport video_;
    decode::MfH264Decoder decoder_;
    render::D3D11Renderer renderer_;
    input::ClientInputCapture input_;
};

} // namespace remote::client
