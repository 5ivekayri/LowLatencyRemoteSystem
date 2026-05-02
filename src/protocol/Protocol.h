#pragma once

#include <cstdint>

namespace remote::protocol {

inline constexpr uint32_t ControlMagic = 0x53524C4C; // LLRS
inline constexpr uint32_t VideoMagic = 0x56524C4C;   // LLRV
inline constexpr uint16_t Version = 0;

enum class MessageType : uint16_t {
    Hello = 1,
    HostInfo = 2,
    AuthToken = 3,
    StartStream = 4,
    StopStream = 5,
    RequestKeyFrame = 6,
    SetBitrate = 7,
    InputEvent = 8,
    Stats = 9,
    Ping = 10,
    Pong = 11,
};

enum VideoPacketFlags : uint16_t {
    KeyFrame = 1 << 0,
    EndOfFrame = 1 << 1,
    ConfigPacket = 1 << 2,
};

enum class VideoPayloadFormat : uint32_t {
    DummyBytes = 0,
    RawBGRA8 = 1,
    H264 = 2,
};

#pragma pack(push, 1)
struct MessageHeader {
    uint32_t magic = ControlMagic;
    uint16_t version = Version;
    uint16_t type = 0;
    uint32_t payloadSize = 0;
};

struct PacketHeader {
    uint32_t magic = VideoMagic;
    uint16_t version = Version;
    uint16_t headerSize = sizeof(PacketHeader);
    uint64_t sessionId = 0;
    uint64_t sequence = 0;
    uint64_t timestampUs = 0;
    uint64_t frameId = 0;
    uint16_t fragmentIndex = 0;
    uint16_t fragmentCount = 0;
    uint16_t flags = 0;
    uint16_t payloadSize = 0;
};

struct VideoFrameHeader {
    uint32_t headerSize = sizeof(VideoFrameHeader);
    uint32_t payloadFormat = static_cast<uint32_t>(VideoPayloadFormat::DummyBytes);
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t strideBytes = 0;
    uint64_t frameId = 0;
    uint64_t captureTimestampUs = 0;
    uint32_t payloadSizeBytes = 0;
};
#pragma pack(pop)

static_assert(sizeof(MessageHeader) == 12);
static_assert(sizeof(PacketHeader) == 48);
static_assert(sizeof(VideoFrameHeader) == 40);

inline constexpr size_t TargetUdpMtuSize = 1200;
inline constexpr size_t TargetUdpPayloadSize = TargetUdpMtuSize - sizeof(PacketHeader);

} // namespace remote::protocol
