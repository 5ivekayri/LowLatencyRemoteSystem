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
#include <winsock2.h>
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
        config_.fps = (config_.mode == CaptureMode::Dummy) ? 60 : 10;
    }
    if (config_.mode == CaptureMode::Dummy && !config_.maxMbpsProvided) {
        config_.maxMbps = 1000;
    }
    if (config_.mode == CaptureMode::Dxgi && (config_.width == 0 || config_.height == 0)) {
        config_.width = 640;
        config_.height = 360;
        Log(LogLevel::Warn, "Raw DXGI mode defaults to reduced resolution because uncompressed 1080p is too large.");
    }
    rawStream_.targetWidth = config_.width != 0 ? config_.width : streamWidth_;
    rawStream_.targetHeight = config_.height != 0 ? config_.height : streamHeight_;
    rawStream_.targetFps = config_.fps;
    rawStream_.maxMbps = config_.maxMbps;
    rawStream_.mtuPayloadBytes = config_.mtuPayloadBytes;
    rawStream_.packetPacingEnabled = config_.packetPacingEnabled;
    rawStream_.udpSendBufferBytes = config_.udpSendBufferBytes;

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
    Logf(LogLevel::Info,
         "Raw stream config: target={}x{} fps={} maxMbps={} mtuPayloadBytes={} pacing={} udpSendBufferBytes={}",
         rawStream_.targetWidth,
         rawStream_.targetHeight,
         rawStream_.targetFps,
         rawStream_.maxMbps,
         rawStream_.mtuPayloadBytes,
         rawStream_.packetPacingEnabled ? "true" : "false",
         rawStream_.udpSendBufferBytes);
    Logf(LogLevel::Info, "Selected frame source: {}", SourceName(config_.mode));

    auto budgetPreflight = ValidateRawStreamBudget();
    if (!budgetPreflight) {
        return budgetPreflight;
    }

    auto source = InitializeFrameSource();
    if (!source) {
        Logf(LogLevel::Error, "FATAL frame source initialization failed: {}", source.error());
        return source;
    }
    if (streamWidth_ == 0 || streamHeight_ == 0) {
        Log(LogLevel::Error, "FATAL frame source produced invalid 0x0 dimensions");
        return Result::Fail("invalid frame source dimensions");
    }
    auto budget = ValidateRawStreamBudget();
    if (!budget) {
        return budget;
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
        sourceWidth_ = capture_.width();
        sourceHeight_ = capture_.height();
        streamWidth_ = rawStream_.targetWidth;
        streamHeight_ = rawStream_.targetHeight;
        Logf(LogLevel::Info,
             "source=DXGI initialized capture={}x{} stream={}x{} format={}",
             sourceWidth_,
             sourceHeight_,
             streamWidth_,
             streamHeight_,
             static_cast<uint32_t>(capture_.format()));
    } else {
        if (config_.width != 0) {
            streamWidth_ = config_.width;
        }
        if (config_.height != 0) {
            streamHeight_ = config_.height;
        }
        sourceWidth_ = streamWidth_;
        sourceHeight_ = streamHeight_;
        rawStream_.targetWidth = streamWidth_;
        rawStream_.targetHeight = streamHeight_;
        Logf(LogLevel::Info, "source=Dummy initialized dimensions={}x{}", streamWidth_, streamHeight_);
    }
    frameSourceInitialized_ = true;
    return Result::Ok();
}

Result HostApp::ValidateRawStreamBudget() const {
    if (rawStream_.targetWidth == 0 || rawStream_.targetHeight == 0 || config_.fps == 0 || config_.maxMbps == 0) {
        return Result::Fail("raw stream dimensions/fps/maxMbps must be non-zero");
    }
    const double estimatedMbps = static_cast<double>(rawStream_.targetWidth) * static_cast<double>(rawStream_.targetHeight) * 4.0 * static_cast<double>(config_.fps) * 8.0 / 1'000'000.0;
    if (estimatedMbps > static_cast<double>(config_.maxMbps)) {
        Logf(LogLevel::Error,
             "FATAL Raw stream exceeds maxMbps. Requested {:.1f} Mbps, limit {} Mbps. Lower width/height/fps or raise --max-mbps.",
             estimatedMbps,
             config_.maxMbps);
        return Result::Fail("raw stream exceeds configured maxMbps");
    }
    Logf(LogLevel::Info, "Raw stream budget OK: estimated={:.1f}Mbps limit={}Mbps", estimatedMbps, config_.maxMbps);
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
    auto sendBuffer = video_.SetBufferSizes(8 * 1024 * 1024, static_cast<int>(config_.udpSendBufferBytes));
    if (!sendBuffer) {
        return sendBuffer;
    }
    auto remote = video_.SetRemote(clientUdpAddress_, clientUdpPort_);
    if (!remote) {
        return remote;
    }

    uint64_t sequence = 1;
    uint64_t frameId = 1;
    uint64_t sentPacketsThisSecond = 0;
    uint64_t sentFramesThisSecond = 0;
    uint64_t capturedFramesThisSecond = 0;
    uint64_t sentBytesThisSecond = 0;
    uint64_t droppedFramesDueToBackpressure = 0;
    uint64_t sendtoErrors = 0;
    int lastWin32Error = 0;
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
    encode::DummyRawFrameGenerator dummyGenerator(streamWidth_, streamHeight_);
    const auto frameInterval = std::chrono::microseconds(1'000'000 / config_.fps);
    Logf(LogLevel::Info,
         "Video loop started: sourceKind={} capture={}x{} stream={}x{} fps={} maxMbps={} udpTarget={}:{}",
         config_.mode == CaptureMode::Dxgi ? "DXGI" : "Dummy",
         sourceWidth_,
         sourceHeight_,
         streamWidth_,
         streamHeight_,
         config_.fps,
         config_.maxMbps,
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
            ++capturedFramesThisSecond;
            if (cap.width != streamWidth_ || cap.height != streamHeight_) {
                frameBytes = ResizeBGRA8Nearest(AsBytes(cap.bytes), cap.width, cap.height, cap.strideBytes, streamWidth_, streamHeight_);
                if (frameBytes.empty()) {
                    return Result::Fail("resize BGRA frame failed");
                }
                // TODO: replace this CPU scaler with a GPU/D3D11 scaling path before H.264 encode.
            } else {
                frameBytes = cap.bytes;
            }
            frameHeader.payloadFormat = static_cast<uint32_t>(protocol::VideoPayloadFormat::RawBGRA8);
            frameHeader.width = streamWidth_;
            frameHeader.height = streamHeight_;
            frameHeader.strideBytes = streamWidth_ * 4;
            frameHeader.frameId = frameId;
            frameHeader.captureTimestampUs = cap.timestampUs;
            frameHeader.payloadSizeBytes = static_cast<uint32_t>(frameBytes.size());
        } else {
            ++capturedFramesThisSecond;
            auto rawFrame = dummyGenerator.Generate(frameId);
            frameHeader = rawFrame.header;
            frameBytes = std::move(rawFrame.bytes);
        }

        const uint64_t sequenceBefore = sequence;
        auto sentFrame = SendRawFrame(frameHeader, AsBytes(frameBytes), sequence);
        if (!sentFrame) {
            Logf(LogLevel::Warn, "Dropping frame {} after UDP send failure: {}", frameHeader.frameId, sentFrame.error());
            ++droppedFramesDueToBackpressure;
            ++sendtoErrors;
            lastWin32Error = video_.lastSendError();
            ++frameId;
            std::this_thread::sleep_until(nextFrameAt);
            continue;
        }
        if (sentFrame.value().dropped) {
            ++droppedFramesDueToBackpressure;
            ++sendtoErrors;
            lastWin32Error = sentFrame.value().lastWin32Error;
            ++frameId;
            std::this_thread::sleep_until(nextFrameAt);
            continue;
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
        sentPacketsThisSecond += sentFrame.value().packetsSent;
        sentBytesThisSecond += sentFrame.value().bytesSent;

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
                 "UDP send stats: capturedFrames/s={} sentFrames/s={} sentPackets/s={} sentMbps={:.1f} droppedFramesDueToBackpressure={} sendtoErrors={} lastWin32Error={} nextSequence={} captureCopyAvgMs={:.2f}",
                 capturedFramesThisSecond,
                 sentFramesThisSecond,
                 sentPacketsThisSecond,
                 static_cast<double>(sentBytesThisSecond) * 8.0 / 1'000'000.0,
                 droppedFramesDueToBackpressure,
                 sendtoErrors,
                 lastWin32Error,
                 sequence,
                 averageCopyMs);
            capturedFramesThisSecond = 0;
            sentFramesThisSecond = 0;
            sentPacketsThisSecond = 0;
            sentBytesThisSecond = 0;
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

ResultT<HostApp::SendFrameResult> HostApp::SendRawFrame(const protocol::VideoFrameHeader& frameHeader, std::span<const std::byte> payload, uint64_t& sequence) {
    SendFrameResult result;
    ByteBuffer framePayload;
    framePayload.reserve(sizeof(protocol::VideoFrameHeader) + payload.size());
    protocol::AppendPod(framePayload, frameHeader);
    framePayload.insert(framePayload.end(), payload.begin(), payload.end());

    const size_t fragmentPayloadSize = std::min<size_t>(config_.mtuPayloadBytes, protocol::TargetUdpPayloadSize);
    const uint16_t fragmentCount = static_cast<uint16_t>((framePayload.size() + fragmentPayloadSize - 1) / fragmentPayloadSize);
    if (fragmentCount > 2000) {
        Logf(LogLevel::Warn, "Raw frame fragmentCount={} is very high; reduce resolution/FPS or enable H.264 later.", fragmentCount);
    }
    if (pacingLast_ == std::chrono::steady_clock::time_point{}) {
        pacingLast_ = std::chrono::steady_clock::now();
        const double bytesPerSecond = static_cast<double>(config_.maxMbps) * 1'000'000.0 / 8.0;
        pacingTokens_ = bytesPerSecond * 0.050;
    }
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

        const size_t packetBytes = sizeof(protocol::PacketHeader) + payloadSize;
        if (config_.packetPacingEnabled) {
            const double bytesPerSecond = static_cast<double>(config_.maxMbps) * 1'000'000.0 / 8.0;
            const double capacity = bytesPerSecond * 0.050;
            while (pacingTokens_ < static_cast<double>(packetBytes)) {
                const auto now = std::chrono::steady_clock::now();
                const double elapsed = std::chrono::duration<double>(now - pacingLast_).count();
                pacingLast_ = now;
                pacingTokens_ = std::min(capacity, pacingTokens_ + elapsed * bytesPerSecond);
                if (pacingTokens_ < static_cast<double>(packetBytes)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            pacingTokens_ -= static_cast<double>(packetBytes);
        }

        auto sent = video_.SendPacket(header, std::span<const std::byte>(framePayload.data() + offset, payloadSize));
        if (!sent) {
            result.lastWin32Error = video_.lastSendError();
            result.dropped = true;
            if (result.lastWin32Error == WSAENOBUFS) {
                Log(LogLevel::Warn, "UDP send buffer exhausted; dropping frame. Reduce resolution/FPS/max-mbps.");
            } else {
                Logf(LogLevel::Warn, "UDP send failed; dropping current frame. Win32Error={}", result.lastWin32Error);
            }
            return ResultT<SendFrameResult>::Ok(result);
        }
        ++result.packetsSent;
        result.bytesSent += packetBytes;
    }
    return ResultT<SendFrameResult>::Ok(result);
}

} // namespace remote::host
