#include "HostApp.h"

#include "common/Clock.h"
#include "common/Log.h"
#include "common/RawFrameUtils.h"
#include "encode/DummyRawFrameGenerator.h"
#include "protocol/Serializer.h"

#include <array>
#include <atomic>
#include <chrono>
#include <conio.h>
#include <cstring>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>
#include <windows.h>

namespace remote::host {

namespace {

const char* CaptureModeName(CaptureMode mode) noexcept {
    switch (mode) {
    case CaptureMode::Dummy:
        return "dummy";
    case CaptureMode::Dxgi:
        return "dxgi";
    }
    return "unknown";
}

const char* SourceName(CaptureMode mode) noexcept {
    switch (mode) {
    case CaptureMode::Dummy:
        return "DummyRawFrameGenerator";
    case CaptureMode::Dxgi:
        return "DxgiDuplicator";
    }
    return "unknown";
}

} // namespace

HostApp::HostApp(HostConfig config) : config_(std::move(config)) {}

Result HostApp::Initialize() {
    if (config_.token.empty()) {
        return Result::Fail("host requires a non-empty pairing token");
    }

    if (config_.fps == 0) {
        config_.fps = (config_.mode == CaptureMode::Dummy) ? 60 : 30;
    }

    Log(LogLevel::Info, "Host is visible and requires explicit token pairing");
    Logf(LogLevel::Info,
         "Parsed host mode: {} adapter={} output={} targetFps={} requestedSize={}x{} bind={}",
         CaptureModeName(config_.mode),
         config_.adapterIndex,
         config_.outputIndex,
         config_.fps,
         config_.width,
         config_.height,
         config_.bindAddress);
    Logf(LogLevel::Info, "Selected frame source: {}", SourceName(config_.mode));

    auto source = InitializeFrameSource();
    if (!source) {
        Logf(LogLevel::Error, "FATAL frame source initialization failed: {}", source.error());
        return source;
    }
    if (streamWidth_ == 0 || streamHeight_ == 0) {
        Log(LogLevel::Error, "FATAL frame source produced invalid 0x0 dimensions");
        return Result::Fail("invalid frame source dimensions");
    }

    if (config_.bindAddress == "0.0.0.0") {
        Log(LogLevel::Warn, "LAN mode enabled. Make sure pairing token is set and firewall allows the port.");
    }

    auto tcp = control_.Listen(config_.bindAddress, config_.tcpPort);
    if (!tcp) {
        return tcp;
    }

    Log(LogLevel::Info, "Video source initialized before handshake so HostInfo cannot be 0x0");

    return Result::Ok();
}

Result HostApp::InitializeFrameSource() {
    if (frameSourceInitialized_) {
        return Result::Ok();
    }
    if (config_.mode == CaptureMode::Dxgi) {
        Logf(LogLevel::Info, "source=DXGI initializing DxgiDuplicator adapter={} output={} targetFps={}", config_.adapterIndex, config_.outputIndex, config_.fps);
        auto captureInit = capture_.Initialize(config_.adapterIndex, config_.outputIndex);
        if (!captureInit) {
            return captureInit;
        }
        streamWidth_ = capture_.width();
        streamHeight_ = capture_.height();
        Logf(LogLevel::Info, "source=DXGI initialized dimensions={}x{} format={}", streamWidth_, streamHeight_, static_cast<uint32_t>(capture_.format()));
    } else {
        if (config_.width != 0) {
            streamWidth_ = config_.width;
        }
        if (config_.height != 0) {
            streamHeight_ = config_.height;
        }
        Logf(LogLevel::Info, "source=Dummy initialized dimensions={}x{}", streamWidth_, streamHeight_);
    }
    frameSourceInitialized_ = true;
    return Result::Ok();
}

int HostApp::Run() {
    Logf(LogLevel::Info, "remote_host skeleton running. TCP={}, UDP={}", config_.tcpPort, config_.udpPort);
    const auto handshake = RunHandshake();
    if (!handshake) {
        Logf(LogLevel::Error, "Handshake failed: {}", handshake.error());
        Log(LogLevel::Info, "Press Enter to stop remote_host skeleton");
        std::cin.get();
        return 1;
    }
    const auto video = RunRawVideoLoop();
    if (!video) {
        Logf(LogLevel::Error, "Raw UDP video loop failed: {}", video.error());
        Log(LogLevel::Info, "Press Enter to close remote_host");
        std::cin.get();
        return 1;
    }
    return 0;
}

Result HostApp::RunHandshake() {
    Log(LogLevel::Info, "Waiting for TCP Hello");
    auto accepted = control_.Accept();
    if (!accepted) {
        return accepted;
    }

    auto received = control_.ReceiveMessage();
    if (!received) {
        return Result::Fail(received.error());
    }

    if (static_cast<protocol::MessageType>(received.value().header.type) != protocol::MessageType::Hello) {
        return Result::Fail("expected Hello message");
    }

    auto hello = protocol::DeserializeHello(AsBytes(received.value().payload));
    if (!hello) {
        return Result::Fail(hello.error());
    }

    Logf(LogLevel::Info,
         "Received Hello: protocolVersion={} clientName='{}' udpPort={}",
         hello.value().protocolVersion,
         hello.value().clientName,
         hello.value().udpPort);

    if (hello.value().protocolVersion != protocol::Version) {
        return Result::Fail("unsupported protocol version");
    }
    if (hello.value().udpPort == 0) {
        return Result::Fail("client Hello udpPort must be non-zero");
    }
    if (hello.value().token != config_.token) {
        Log(LogLevel::Warn, "Pairing token rejected");
        return Result::Fail("invalid pairing token");
    }
    clientUdpPort_ = hello.value().udpPort;
    clientUdpAddress_ = control_.peerAddress();
    Logf(LogLevel::Info, "Client UDP target {}:{}", clientUdpAddress_, clientUdpPort_);
    Log(LogLevel::Info, "Pairing token accepted");

    protocol::HostInfo info;
    std::array<char, MAX_COMPUTERNAME_LENGTH + 1> computerName{};
    DWORD computerNameSize = static_cast<DWORD>(computerName.size());
    if (GetComputerNameA(computerName.data(), &computerNameSize) != 0) {
        info.hostName.assign(computerName.data(), computerNameSize);
    } else {
        LogWin32Error("GetComputerNameA", GetLastError());
        info.hostName = "remote_host";
    }
    if (streamWidth_ == 0 || streamHeight_ == 0) {
        Log(LogLevel::Error, "FATAL refusing to send HostInfo with 0x0 dimensions");
        return Result::Fail("invalid HostInfo dimensions");
    }
    info.width = streamWidth_;
    info.height = streamHeight_;
    info.refreshHz = 60;

    auto payload = protocol::SerializeHostInfo(info);
    auto sent = control_.SendMessage(protocol::MessageType::HostInfo, AsBytes(payload));
    if (!sent) {
        return sent;
    }

    Logf(LogLevel::Info, "Sent HostInfo: hostName='{}' {}x{}@{}Hz", info.hostName, info.width, info.height, info.refreshHz);
    return Result::Ok();
}

Result HostApp::RunRawVideoLoop() {
    auto udp = video_.Bind(config_.bindAddress, config_.udpPort);
    if (!udp) {
        return udp;
    }
    auto remote = video_.SetRemote(clientUdpAddress_, clientUdpPort_);
    if (!remote) {
        return remote;
    }

    uint64_t sequence = 1;
    uint64_t frameId = 1;
    uint64_t sentPacketsThisSecond = 0;
    uint64_t sentFramesThisSecond = 0;
    uint64_t copyTotalUsThisSecond = 0;
    uint64_t copySamplesThisSecond = 0;
    if (config_.mode == CaptureMode::Dxgi) {
        Logf(LogLevel::Info, "source=DXGI selected adapter={} output={}. No silent fallback to dummy is allowed.", config_.adapterIndex, config_.outputIndex);
    } else {
        if (config_.width != 0) {
            streamWidth_ = config_.width;
        }
        if (config_.height != 0) {
            streamHeight_ = config_.height;
        }
        Logf(LogLevel::Info,
             "source=Dummy selected DummyRawFrameGenerator: {}x{} targetFps={}",
             streamWidth_,
             streamHeight_,
             config_.fps);
    }

    std::atomic_bool running = true;
    std::thread inputThread([&running]() {
        Log(LogLevel::Info, "Raw BGRA UDP video running. Press Enter to stop remote_host");
        while (running) {
            if (_kbhit()) {
                const int key = _getch();
                if (key == '\r' || key == '\n') {
                    running = false;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    auto nextFrameAt = std::chrono::steady_clock::now();
    auto nextStatsAt = nextFrameAt + std::chrono::seconds(1);
    const auto frameInterval = std::chrono::microseconds(1'000'000 / config_.fps);
    Logf(LogLevel::Info,
         "Video loop started: sourceKind={} width={} height={} fps={} udpTarget={}:{}",
         config_.mode == CaptureMode::Dxgi ? "DXGI" : "Dummy",
         streamWidth_,
         streamHeight_,
         config_.fps,
         clientUdpAddress_,
         clientUdpPort_);
    uint32_t loggedSentFrames = 0;
    auto lastFrameSentAt = std::chrono::steady_clock::now();

    while (running) {
        nextFrameAt += frameInterval;

        protocol::VideoFrameHeader frameHeader;
        ByteBuffer frameBytes;
        if (config_.mode == CaptureMode::Dxgi) {
            const uint64_t copyStartUs = NowUs();
            auto captured = capture_.AcquireFrame(16, frameId);
            if (!captured) {
                running = false;
                if (inputThread.joinable()) {
                    inputThread.join();
                }
                return Result::Fail(captured.error());
            }
            if (!captured.value().has_value()) {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastFrameSentAt > std::chrono::seconds(1)) {
                    Log(LogLevel::Warn, "No frames sent for >1s; DXGI AcquireNextFrame is timing out with no desktop update");
                    lastFrameSentAt = now;
                }
                std::this_thread::sleep_until(nextFrameAt);
                continue;
            }
            const uint64_t copyDoneUs = NowUs();
            copyTotalUsThisSecond += copyDoneUs - copyStartUs;
            ++copySamplesThisSecond;

            const auto& cap = *captured.value();
            frameHeader.payloadFormat = static_cast<uint32_t>(protocol::VideoPayloadFormat::RawBGRA8);
            frameHeader.width = cap.width;
            frameHeader.height = cap.height;
            frameHeader.strideBytes = cap.strideBytes;
            frameHeader.frameId = frameId;
            frameHeader.captureTimestampUs = cap.timestampUs;
            frameHeader.payloadSizeBytes = static_cast<uint32_t>(cap.bytes.size());
            frameBytes = cap.bytes;
        } else {
            encode::DummyRawFrameGenerator generator(streamWidth_, streamHeight_);
            auto rawFrame = generator.Generate(frameId);
            frameHeader = rawFrame.header;
            frameBytes = std::move(rawFrame.bytes);
        }

        const uint64_t sequenceBefore = sequence;
        auto sentFrame = SendRawFrame(frameHeader, AsBytes(frameBytes), sequence);
        if (!sentFrame) {
            running = false;
            if (inputThread.joinable()) {
                inputThread.join();
            }
            return sentFrame;
        }
        if (loggedSentFrames < 3) {
            Logf(LogLevel::Info,
                 "Sent frame: frameId={} source={} format={} {}x{} strideBytes={} payloadBytes={} fragmentCount={}",
                 frameHeader.frameId,
                 config_.mode == CaptureMode::Dxgi ? "DXGI" : "Dummy",
                 frameHeader.payloadFormat,
                 frameHeader.width,
                 frameHeader.height,
                 frameHeader.strideBytes,
                 frameHeader.payloadSizeBytes,
                 sequence - sequenceBefore);
            ++loggedSentFrames;
        }
        lastFrameSentAt = std::chrono::steady_clock::now();
        sentPacketsThisSecond += sequence - sequenceBefore;

        ++sentFramesThisSecond;
        ++frameId;

        const auto now = std::chrono::steady_clock::now();
        if (now - lastFrameSentAt > std::chrono::seconds(1)) {
            Log(LogLevel::Warn, "No frames sent for >1s; source may be waiting for DXGI updates or packet send failed");
            lastFrameSentAt = now;
        }
        if (now >= nextStatsAt) {
            const double averageCopyMs = copySamplesThisSecond == 0 ? 0.0 : static_cast<double>(copyTotalUsThisSecond) / static_cast<double>(copySamplesThisSecond) / 1000.0;
            Logf(LogLevel::Info,
                 "UDP send stats: frames/s={} packets/s={} nextSequence={} captureCopyAvgMs={:.2f}",
                 sentFramesThisSecond,
                 sentPacketsThisSecond,
                 sequence,
                 averageCopyMs);
            sentFramesThisSecond = 0;
            sentPacketsThisSecond = 0;
            copyTotalUsThisSecond = 0;
            copySamplesThisSecond = 0;
            nextStatsAt = now + std::chrono::seconds(1);
        }

        std::this_thread::sleep_until(nextFrameAt);
    }

    if (inputThread.joinable()) {
        inputThread.join();
    }
    return Result::Ok();
}

Result HostApp::SendRawFrame(const protocol::VideoFrameHeader& frameHeader, std::span<const std::byte> payload, uint64_t& sequence) {
    ByteBuffer framePayload;
    framePayload.reserve(sizeof(protocol::VideoFrameHeader) + payload.size());
    protocol::AppendPod(framePayload, frameHeader);
    framePayload.insert(framePayload.end(), payload.begin(), payload.end());

    const size_t fragmentPayloadSize = protocol::TargetUdpPayloadSize;
    const uint16_t fragmentCount = static_cast<uint16_t>((framePayload.size() + fragmentPayloadSize - 1) / fragmentPayloadSize);
    for (uint16_t fragmentIndex = 0; fragmentIndex < fragmentCount; ++fragmentIndex) {
        const size_t offset = static_cast<size_t>(fragmentIndex) * fragmentPayloadSize;
        const size_t bytesLeft = framePayload.size() - offset;
        const size_t payloadSize = (bytesLeft < fragmentPayloadSize) ? bytesLeft : fragmentPayloadSize;

        protocol::PacketHeader header;
        header.sessionId = sessionId_;
        header.sequence = sequence++;
        header.timestampUs = NowUs();
        header.frameId = frameHeader.frameId;
        header.fragmentIndex = fragmentIndex;
        header.fragmentCount = fragmentCount;
        header.flags = (fragmentIndex + 1 == fragmentCount) ? protocol::EndOfFrame : 0;
        header.payloadSize = static_cast<uint16_t>(payloadSize);

        auto sent = video_.SendPacket(header, std::span<const std::byte>(framePayload.data() + offset, payloadSize));
        if (!sent) {
            return sent;
        }
    }
    return Result::Ok();
}

} // namespace remote::host
