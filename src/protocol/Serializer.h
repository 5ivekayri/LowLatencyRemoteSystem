#pragma once

#include "common/ByteBuffer.h"
#include "common/Result.h"
#include "protocol/Messages.h"
#include "protocol/Protocol.h"

#include <cstring>
#include <span>
#include <string_view>
#include <utility>

namespace remote::protocol {

template <typename T>
inline void AppendPod(ByteBuffer& out, const T& value) {
    const auto* first = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), first, first + sizeof(T));
}

inline void AppendString(ByteBuffer& out, std::string_view text) {
    const auto size = static_cast<uint16_t>(text.size());
    AppendPod(out, size);
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    out.insert(out.end(), first, first + text.size());
}

template <typename T>
inline bool ReadPod(std::span<const std::byte> bytes, size_t& offset, T& value) {
    if (bytes.size() - offset < sizeof(T)) {
        return false;
    }
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

inline bool ReadString(std::span<const std::byte> bytes, size_t& offset, std::string& value) {
    uint16_t size = 0;
    if (!ReadPod(bytes, offset, size) || bytes.size() - offset < size) {
        return false;
    }
    value.assign(reinterpret_cast<const char*>(bytes.data() + offset), size);
    offset += size;
    return true;
}

inline ByteBuffer MakeControlMessage(MessageType type, std::span<const std::byte> payload) {
    ByteBuffer out;
    MessageHeader header;
    header.type = static_cast<uint16_t>(type);
    header.payloadSize = static_cast<uint32_t>(payload.size());
    AppendPod(out, header);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

inline bool ValidateControlHeader(const MessageHeader& header) {
    return header.magic == ControlMagic && header.version == Version;
}

inline bool ValidatePacketHeader(const PacketHeader& header) {
    return header.magic == VideoMagic && header.version == Version && header.headerSize == sizeof(PacketHeader);
}

inline ByteBuffer SerializeHello(const Hello& hello) {
    ByteBuffer out;
    AppendPod(out, hello.protocolVersion);
    AppendPod(out, hello.udpPort);
    AppendString(out, hello.clientName);
    AppendString(out, hello.token);
    return out;
}

inline ResultT<Hello> DeserializeHello(std::span<const std::byte> payload) {
    Hello hello;
    size_t offset = 0;
    if (!ReadPod(payload, offset, hello.protocolVersion) ||
        !ReadPod(payload, offset, hello.udpPort) ||
        !ReadString(payload, offset, hello.clientName) ||
        !ReadString(payload, offset, hello.token)) {
        return ResultT<Hello>::Fail("invalid Hello payload");
    }
    if (offset != payload.size()) {
        return ResultT<Hello>::Fail("Hello payload has trailing bytes");
    }
    return ResultT<Hello>::Ok(std::move(hello));
}

inline ByteBuffer SerializeHostInfo(const HostInfo& info) {
    ByteBuffer out;
    AppendPod(out, info.width);
    AppendPod(out, info.height);
    AppendPod(out, info.refreshHz);
    AppendString(out, info.hostName);
    return out;
}

inline ResultT<HostInfo> DeserializeHostInfo(std::span<const std::byte> payload) {
    HostInfo info;
    size_t offset = 0;
    if (!ReadPod(payload, offset, info.width) ||
        !ReadPod(payload, offset, info.height) ||
        !ReadPod(payload, offset, info.refreshHz) ||
        !ReadString(payload, offset, info.hostName)) {
        return ResultT<HostInfo>::Fail("invalid HostInfo payload");
    }
    if (offset != payload.size()) {
        return ResultT<HostInfo>::Fail("HostInfo payload has trailing bytes");
    }
    return ResultT<HostInfo>::Ok(std::move(info));
}

} // namespace remote::protocol
