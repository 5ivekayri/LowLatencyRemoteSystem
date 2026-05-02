#pragma once

#include "common/ByteBuffer.h"
#include "common/Result.h"
#include "protocol/Protocol.h"
#include "transport/ITransport.h"

#include <string>
#include <winsock2.h>

namespace remote::transport {

class WinsockRuntime {
public:
    WinsockRuntime() = default;
    ~WinsockRuntime();

    WinsockRuntime(const WinsockRuntime&) = delete;
    WinsockRuntime& operator=(const WinsockRuntime&) = delete;

    Result Initialize();

private:
    bool initialized_ = false;
};

class TcpControlChannel final : public ITransport {
public:
    struct ReceivedMessage {
        protocol::MessageHeader header;
        ByteBuffer payload;
    };

    TcpControlChannel() = default;
    ~TcpControlChannel() override;

    TcpControlChannel(const TcpControlChannel&) = delete;
    TcpControlChannel& operator=(const TcpControlChannel&) = delete;

    Result Start() override;
    void Stop() override;

    Result Listen(uint16_t port);
    Result Listen(const std::string& bindAddress, uint16_t port);
    Result Accept();
    Result Connect(const std::string& host, uint16_t port);
    Result SendMessage(protocol::MessageType type, std::span<const std::byte> payload);
    ResultT<ReceivedMessage> ReceiveMessage();
    Result SetTimeouts(int receiveTimeoutMs, int sendTimeoutMs);
    [[nodiscard]] const std::string& peerAddress() const noexcept { return peerAddress_; }

private:
    Result SendAll(std::span<const std::byte> bytes);
    Result RecvAll(std::span<std::byte> bytes);

    WinsockRuntime winsock_;
    SOCKET listenSocket_ = INVALID_SOCKET;
    SOCKET socket_ = INVALID_SOCKET;
    std::string peerAddress_;
};

} // namespace remote::transport
