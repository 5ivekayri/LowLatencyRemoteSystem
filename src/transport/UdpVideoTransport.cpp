#include "transport/UdpVideoTransport.h"

#include "common/Log.h"
#include "protocol/Serializer.h"

#include <cstring>
#include <utility>
#include <ws2tcpip.h>

namespace remote::transport {

UdpVideoTransport::~UdpVideoTransport() {
    Stop();
}

Result UdpVideoTransport::Start() {
    return winsock_.Initialize();
}

void UdpVideoTransport::Stop() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    hasRemote_ = false;
}

Result UdpVideoTransport::Bind(uint16_t port) {
    return Bind("0.0.0.0", port);
}

Result UdpVideoTransport::Bind(const std::string& bindAddress, uint16_t port) {
    auto started = Start();
    if (!started) {
        return started;
    }

    Stop();
    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        LogWin32Error("socket(UDP)", WSAGetLastError());
        return Result::Fail("create UDP socket failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (InetPtonA(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1) {
        LogWin32Error("InetPtonA(UDP bind)", WSAGetLastError());
        return Result::Fail("invalid UDP bind address");
    }

    if (bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LogWin32Error("bind(UDP)", WSAGetLastError());
        return Result::Fail("bind UDP socket failed");
    }

    auto buffers = SetBufferSizes(8 * 1024 * 1024, 8 * 1024 * 1024);
    if (!buffers) {
        return buffers;
    }
    auto timeout = SetReceiveTimeout(100);
    if (!timeout) {
        return timeout;
    }

    Logf(LogLevel::Info, "UDP video bound on {}:{}", bindAddress, port);
    return Result::Ok();
}

Result UdpVideoTransport::SetRemote(const std::string& host, uint16_t port) {
    remote_ = {};
    remote_.sin_family = AF_INET;
    remote_.sin_port = htons(port);
    if (InetPtonA(AF_INET, host.c_str(), &remote_.sin_addr) != 1) {
        LogWin32Error("InetPtonA(UDP remote)", WSAGetLastError());
        return Result::Fail("only IPv4 literal UDP remotes are supported in the skeleton");
    }
    hasRemote_ = true;
    return Result::Ok();
}

Result UdpVideoTransport::SendPacket(const protocol::PacketHeader& header, std::span<const std::byte> payload) {
    lastSendError_ = 0;
    if (socket_ == INVALID_SOCKET) {
        return Result::Fail("UDP socket is not bound");
    }
    if (!hasRemote_) {
        return Result::Fail("UDP remote endpoint is not configured");
    }
    if (!protocol::ValidatePacketHeader(header) || header.payloadSize != payload.size()) {
        return Result::Fail("invalid UDP packet header");
    }

    ByteBuffer packet;
    protocol::AppendPod(packet, header);
    packet.insert(packet.end(), payload.begin(), payload.end());

    const int sent = sendto(
        socket_,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&remote_),
        sizeof(remote_));
    if (sent == SOCKET_ERROR) {
        lastSendError_ = WSAGetLastError();
        LogWin32Error("sendto(UDP)", lastSendError_);
        return Result::Fail("send UDP packet failed");
    }

    return Result::Ok();
}

ResultT<UdpVideoTransport::ReceivedPacket> UdpVideoTransport::ReceivePacket() {
    if (socket_ == INVALID_SOCKET) {
        return ResultT<ReceivedPacket>::Fail("UDP socket is not bound");
    }

    ByteBuffer buffer(protocol::TargetUdpMtuSize);
    ReceivedPacket packet;
    int remoteSize = sizeof(packet.remote);
    const int received = recvfrom(
        socket_,
        reinterpret_cast<char*>(buffer.data()),
        static_cast<int>(buffer.size()),
        0,
        reinterpret_cast<sockaddr*>(&packet.remote),
        &remoteSize);

    if (received == SOCKET_ERROR) {
        const int error = WSAGetLastError();
        if (error == WSAETIMEDOUT) {
            return ResultT<ReceivedPacket>::Fail("UDP receive timeout");
        }
        LogWin32Error("recvfrom(UDP)", error);
        return ResultT<ReceivedPacket>::Fail("receive UDP packet failed");
    }
    if (received < static_cast<int>(sizeof(protocol::PacketHeader))) {
        return ResultT<ReceivedPacket>::Fail("UDP packet is smaller than header");
    }

    size_t offset = 0;
    if (!protocol::ReadPod(AsBytes(buffer).first(static_cast<size_t>(received)), offset, packet.header) ||
        !protocol::ValidatePacketHeader(packet.header)) {
        return ResultT<ReceivedPacket>::Fail("invalid UDP packet header");
    }
    if (packet.header.payloadSize != static_cast<uint16_t>(received - sizeof(protocol::PacketHeader))) {
        return ResultT<ReceivedPacket>::Fail("UDP payload size mismatch");
    }

    packet.payload.resize(packet.header.payloadSize);
    std::memcpy(packet.payload.data(), buffer.data() + sizeof(protocol::PacketHeader), packet.payload.size());
    return ResultT<ReceivedPacket>::Ok(std::move(packet));
}

Result UdpVideoTransport::SetReceiveTimeout(int timeoutMs) {
    if (socket_ == INVALID_SOCKET) {
        return Result::Fail("UDP socket is not bound");
    }
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs)) == SOCKET_ERROR) {
        LogWin32Error("setsockopt(UDP SO_RCVTIMEO)", WSAGetLastError());
        return Result::Fail("configure UDP receive timeout failed");
    }
    return Result::Ok();
}

Result UdpVideoTransport::SetBufferSizes(int receiveBytes, int sendBytes) {
    if (socket_ == INVALID_SOCKET) {
        return Result::Fail("UDP socket is not bound");
    }
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receiveBytes), sizeof(receiveBytes)) == SOCKET_ERROR) {
        LogWin32Error("setsockopt(UDP SO_RCVBUF)", WSAGetLastError());
        return Result::Fail("configure UDP receive buffer failed");
    }
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sendBytes), sizeof(sendBytes)) == SOCKET_ERROR) {
        LogWin32Error("setsockopt(UDP SO_SNDBUF)", WSAGetLastError());
        return Result::Fail("configure UDP send buffer failed");
    }
    int actualReceive = 0;
    int actualSend = 0;
    int actualReceiveSize = sizeof(actualReceive);
    int actualSendSize = sizeof(actualSend);
    if (getsockopt(socket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&actualReceive), &actualReceiveSize) == 0 &&
        getsockopt(socket_, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&actualSend), &actualSendSize) == 0) {
        Logf(LogLevel::Info, "UDP socket buffers requested rcv={} snd={} actual rcv={} snd={}", receiveBytes, sendBytes, actualReceive, actualSend);
    }
    return Result::Ok();
}

} // namespace remote::transport
