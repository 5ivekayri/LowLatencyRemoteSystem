#include "transport/TcpControlChannel.h"

#include "common/Log.h"
#include "protocol/Serializer.h"

#include <iterator>
#include <ws2tcpip.h>

namespace remote::transport {

WinsockRuntime::~WinsockRuntime() {
    if (initialized_) {
        WSACleanup();
    }
}

Result WinsockRuntime::Initialize() {
    if (initialized_) {
        return Result::Ok();
    }

    WSADATA data{};
    const int rc = WSAStartup(MAKEWORD(2, 2), &data);
    if (rc != 0) {
        LogWin32Error("WSAStartup", static_cast<DWORD>(rc));
        return Result::Fail("Winsock startup failed");
    }

    initialized_ = true;
    Log(LogLevel::Info, "Winsock initialized");
    return Result::Ok();
}

TcpControlChannel::~TcpControlChannel() {
    Stop();
}

Result TcpControlChannel::Start() {
    return winsock_.Initialize();
}

void TcpControlChannel::Stop() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
}

Result TcpControlChannel::Listen(uint16_t port) {
    return Listen("127.0.0.1", port);
}

Result TcpControlChannel::Listen(const std::string& bindAddress, uint16_t port) {
    auto started = Start();
    if (!started) {
        return started;
    }

    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    listenSocket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket_ == INVALID_SOCKET) {
        LogWin32Error("socket(TCP)", WSAGetLastError());
        return Result::Fail("create TCP socket failed");
    }

    BOOL reuseAddress = TRUE;
    if (setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress)) == SOCKET_ERROR) {
        LogWin32Error("setsockopt(SO_REUSEADDR)", WSAGetLastError());
        return Result::Fail("configure TCP socket reuse failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (InetPtonA(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1) {
        LogWin32Error("InetPtonA(TCP bind)", WSAGetLastError());
        return Result::Fail("invalid TCP bind address");
    }

    if (bind(listenSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LogWin32Error("bind(TCP)", WSAGetLastError());
        return Result::Fail("bind TCP socket failed");
    }
    if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
        LogWin32Error("listen(TCP)", WSAGetLastError());
        return Result::Fail("listen TCP socket failed");
    }

    Logf(LogLevel::Info, "TCP control listening on {}:{}", bindAddress, port);
    return Result::Ok();
}

Result TcpControlChannel::Accept() {
    if (listenSocket_ == INVALID_SOCKET) {
        return Result::Fail("TCP listen socket is not initialized");
    }
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    sockaddr_in clientAddr{};
    int clientAddrLen = sizeof(clientAddr);
    socket_ = accept(listenSocket_, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);
    if (socket_ == INVALID_SOCKET) {
        LogWin32Error("accept(TCP)", WSAGetLastError());
        return Result::Fail("accept TCP client failed");
    }

    char address[INET_ADDRSTRLEN]{};
    InetNtopA(AF_INET, &clientAddr.sin_addr, address, static_cast<DWORD>(std::size(address)));
    peerAddress_ = address;
    Logf(LogLevel::Info, "TCP control accepted client {}:{}", address, ntohs(clientAddr.sin_port));
    return SetTimeouts(5000, 5000);
}

Result TcpControlChannel::Connect(const std::string& host, uint16_t port) {
    auto started = Start();
    if (!started) {
        return started;
    }

    Stop();
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
        LogWin32Error("socket(TCP)", WSAGetLastError());
        return Result::Fail("create TCP socket failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (InetPtonA(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        LogWin32Error("InetPtonA", WSAGetLastError());
        return Result::Fail("only IPv4 literal host addresses are supported in the skeleton");
    }

    if (connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LogWin32Error("connect(TCP)", WSAGetLastError());
        return Result::Fail("connect TCP socket failed");
    }

    Logf(LogLevel::Info, "TCP control connected to {}:{}", host, port);
    return SetTimeouts(5000, 5000);
}

Result TcpControlChannel::SendMessage(protocol::MessageType type, std::span<const std::byte> payload) {
    if (socket_ == INVALID_SOCKET) {
        return Result::Fail("TCP control socket is not connected");
    }

    const auto message = protocol::MakeControlMessage(type, payload);
    return SendAll(AsBytes(message));
}

ResultT<TcpControlChannel::ReceivedMessage> TcpControlChannel::ReceiveMessage() {
    if (socket_ == INVALID_SOCKET) {
        return ResultT<ReceivedMessage>::Fail("TCP control socket is not connected");
    }

    ReceivedMessage message;
    ByteBuffer headerBytes(sizeof(protocol::MessageHeader));
    auto headerResult = RecvAll(AsWritableBytes(headerBytes));
    if (!headerResult) {
        return ResultT<ReceivedMessage>::Fail(headerResult.error());
    }

    size_t offset = 0;
    if (!protocol::ReadPod(AsBytes(headerBytes), offset, message.header) || !protocol::ValidateControlHeader(message.header)) {
        return ResultT<ReceivedMessage>::Fail("invalid TCP control header");
    }
    if (message.header.payloadSize > 64 * 1024) {
        return ResultT<ReceivedMessage>::Fail("TCP control payload is too large");
    }

    message.payload.resize(message.header.payloadSize);
    if (!message.payload.empty()) {
        auto payloadResult = RecvAll(AsWritableBytes(message.payload));
        if (!payloadResult) {
            return ResultT<ReceivedMessage>::Fail(payloadResult.error());
        }
    }
    return ResultT<ReceivedMessage>::Ok(std::move(message));
}

Result TcpControlChannel::SetTimeouts(int receiveTimeoutMs, int sendTimeoutMs) {
    if (socket_ == INVALID_SOCKET) {
        return Result::Fail("TCP control socket is not connected");
    }
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receiveTimeoutMs), sizeof(receiveTimeoutMs)) == SOCKET_ERROR) {
        LogWin32Error("setsockopt(SO_RCVTIMEO)", WSAGetLastError());
        return Result::Fail("configure TCP receive timeout failed");
    }
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeoutMs), sizeof(sendTimeoutMs)) == SOCKET_ERROR) {
        LogWin32Error("setsockopt(SO_SNDTIMEO)", WSAGetLastError());
        return Result::Fail("configure TCP send timeout failed");
    }
    return Result::Ok();
}

Result TcpControlChannel::SendAll(std::span<const std::byte> bytes) {
    const auto* data = reinterpret_cast<const char*>(bytes.data());
    int remaining = static_cast<int>(bytes.size());
    while (remaining > 0) {
        const int sent = send(socket_, data, remaining, 0);
        if (sent == SOCKET_ERROR) {
            LogWin32Error("send(TCP)", WSAGetLastError());
            return Result::Fail("send TCP bytes failed");
        }
        data += sent;
        remaining -= sent;
    }
    return Result::Ok();
}

Result TcpControlChannel::RecvAll(std::span<std::byte> bytes) {
    auto* data = reinterpret_cast<char*>(bytes.data());
    int remaining = static_cast<int>(bytes.size());
    while (remaining > 0) {
        const int received = recv(socket_, data, remaining, 0);
        if (received == 0) {
            return Result::Fail("TCP peer closed connection");
        }
        if (received == SOCKET_ERROR) {
            LogWin32Error("recv(TCP)", WSAGetLastError());
            return Result::Fail("receive TCP bytes failed");
        }
        data += received;
        remaining -= received;
    }
    return Result::Ok();
}

} // namespace remote::transport
