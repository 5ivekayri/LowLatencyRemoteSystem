#pragma once

#include "common/ByteBuffer.h"
#include "common/Result.h"
#include "protocol/Protocol.h"
#include "transport/ITransport.h"
#include "transport/TcpControlChannel.h"

#include <string>
#include <winsock2.h>

namespace remote::transport {

class UdpVideoTransport final : public ITransport {
public:
    struct ReceivedPacket {
        protocol::PacketHeader header;
        ByteBuffer payload;
        sockaddr_in remote{};
    };

    UdpVideoTransport() = default;
    ~UdpVideoTransport() override;

    UdpVideoTransport(const UdpVideoTransport&) = delete;
    UdpVideoTransport& operator=(const UdpVideoTransport&) = delete;

    Result Start() override;
    void Stop() override;

    Result Bind(uint16_t port);
    Result Bind(const std::string& bindAddress, uint16_t port);
    Result SetRemote(const std::string& host, uint16_t port);
    Result SendPacket(const protocol::PacketHeader& header, std::span<const std::byte> payload);
    ResultT<ReceivedPacket> ReceivePacket();
    Result SetReceiveTimeout(int timeoutMs);
    Result SetBufferSizes(int receiveBytes, int sendBytes);
    [[nodiscard]] int lastSendError() const noexcept { return lastSendError_; }

private:
    WinsockRuntime winsock_;
    SOCKET socket_ = INVALID_SOCKET;
    sockaddr_in remote_{};
    bool hasRemote_ = false;
    int lastSendError_ = 0;
};

} // namespace remote::transport
