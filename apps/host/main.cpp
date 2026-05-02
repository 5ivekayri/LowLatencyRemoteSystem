#include "HostApp.h"

#include "capture/DxgiAdapterEnumerator.h"
#include "capture/DxgiDuplicator.h"
#include "common/Log.h"
#include "common/RawFrameUtils.h"
#include "encode/DummyRawFrameGenerator.h"
#include "protocol/Serializer.h"
#include "transport/UdpVideoTransport.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <string_view>
#include <string>
#include <thread>
#include <utility>

namespace {

uint16_t ParsePort(std::string_view value, uint16_t fallback) {
    uint16_t port = fallback;
    std::from_chars(value.data(), value.data() + value.size(), port);
    return port;
}

uint32_t ParseU32(std::string_view value, uint32_t fallback) {
    uint32_t parsed = fallback;
    std::from_chars(value.data(), value.data() + value.size(), parsed);
    return parsed;
}

remote::host::HostConfig ParseArgs(int argc, char** argv) {
    remote::host::HostConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--token" && i + 1 < argc) {
            config.token = argv[++i];
        } else if (arg == "--self-test" && i + 1 < argc) {
            config.selfTest = argv[++i];
        } else if (arg == "--target" && i + 1 < argc) {
            config.udpTarget = argv[++i];
        } else if (arg == "--list-adapters") {
            config.listAdapters = true;
        } else if (arg == "--bind" && i + 1 < argc) {
            config.bindAddress = argv[++i];
        } else if (arg == "--tcp-port" && i + 1 < argc) {
            config.tcpPort = ParsePort(argv[++i], config.tcpPort);
        } else if (arg == "--tcp" && i + 1 < argc) {
            config.tcpPort = ParsePort(argv[++i], config.tcpPort);
        } else if (arg == "--udp-port" && i + 1 < argc) {
            config.udpPort = ParsePort(argv[++i], config.udpPort);
        } else if (arg == "--udp" && i + 1 < argc) {
            config.udpPort = ParsePort(argv[++i], config.udpPort);
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string_view mode = argv[++i];
            if (mode == "dxgi") {
                config.mode = remote::host::CaptureMode::Dxgi;
            } else if (mode == "dummy") {
                config.mode = remote::host::CaptureMode::Dummy;
            } else {
                remote::Logf(remote::LogLevel::Warn, "Unknown --mode '{}', keeping default", std::string(mode));
            }
        } else if (arg == "--adapter" && i + 1 < argc) {
            config.adapterIndex = ParseU32(argv[++i], config.adapterIndex);
        } else if (arg == "--output" && i + 1 < argc) {
            config.outputIndex = ParseU32(argv[++i], config.outputIndex);
        } else if (arg == "--monitor" && i + 1 < argc) {
            config.outputIndex = ParseU32(argv[++i], config.outputIndex);
        } else if (arg == "--fps" && i + 1 < argc) {
            config.fps = ParseU32(argv[++i], config.fps);
        } else if (arg == "--max-mbps" && i + 1 < argc) {
            config.maxMbps = ParseU32(argv[++i], config.maxMbps);
            config.maxMbpsProvided = true;
        } else if (arg == "--udp-send-buffer" && i + 1 < argc) {
            config.udpSendBufferBytes = ParseU32(argv[++i], config.udpSendBufferBytes);
        } else if (arg == "--mtu-payload" && i + 1 < argc) {
            config.mtuPayloadBytes = ParseU32(argv[++i], config.mtuPayloadBytes);
        } else if (arg == "--no-packet-pacing") {
            config.packetPacingEnabled = false;
        } else if (arg == "--width" && i + 1 < argc) {
            config.width = ParseU32(argv[++i], config.width);
        } else if (arg == "--height" && i + 1 < argc) {
            config.height = ParseU32(argv[++i], config.height);
        }
    }
    return config;
}

remote::Result PacketizeAndSend(remote::transport::UdpVideoTransport& udp, const remote::protocol::VideoFrameHeader& header, std::span<const std::byte> payload, uint64_t& sequence, uint64_t& packets, uint64_t& bytes) {
    remote::ByteBuffer framePayload;
    framePayload.reserve(sizeof(remote::protocol::VideoFrameHeader) + payload.size());
    remote::protocol::AppendPod(framePayload, header);
    framePayload.insert(framePayload.end(), payload.begin(), payload.end());
    const size_t fragmentPayloadSize = remote::protocol::TargetUdpPayloadSize;
    const uint16_t fragmentCount = static_cast<uint16_t>((framePayload.size() + fragmentPayloadSize - 1) / fragmentPayloadSize);
    for (uint16_t i = 0; i < fragmentCount; ++i) {
        const size_t offset = static_cast<size_t>(i) * fragmentPayloadSize;
        const size_t payloadSize = std::min(fragmentPayloadSize, framePayload.size() - offset);
        remote::protocol::PacketHeader packet;
        packet.sessionId = 0x4C4C52534D565030ULL;
        packet.sequence = sequence++;
        packet.timestampUs = remote::NowUs();
        packet.frameId = header.frameId;
        packet.fragmentIndex = i;
        packet.fragmentCount = fragmentCount;
        packet.flags = (i + 1 == fragmentCount) ? remote::protocol::EndOfFrame : 0;
        packet.payloadSize = static_cast<uint16_t>(payloadSize);
        auto sent = udp.SendPacket(packet, std::span<const std::byte>(framePayload.data() + offset, payloadSize));
        if (!sent) {
            return sent;
        }
        ++packets;
        bytes += payloadSize;
    }
    return remote::Result::Ok();
}

int RunSelfTest(remote::host::HostConfig& config) {
    const std::filesystem::path artifacts = "artifacts";
    if (config.selfTest == "dummy") {
        remote::encode::DummyRawFrameGenerator generator(640, 360);
        auto frame = generator.Generate(1);
        auto validation = remote::ValidateRawBGRAFrame(frame.header.width, frame.header.height, frame.header.strideBytes, remote::AsBytes(frame.bytes));
        auto saved = remote::SaveBGRA8ToBMP(artifacts / "dummy_frame.bmp", frame.header.width, frame.header.height, frame.header.strideBytes, remote::AsBytes(frame.bytes));
        remote::Logf(validation.valid && saved ? remote::LogLevel::Info : remote::LogLevel::Error,
                     "SELFTEST dummy {} checksum={} uniqueColors={} save={}",
                     validation.valid ? "PASS" : "FAIL",
                     validation.checksum,
                     validation.uniqueSampledColors,
                     saved ? "ok" : saved.error());
        return validation.valid && saved ? 0 : 1;
    }
    if (config.selfTest == "dxgi") {
        remote::capture::DxgiDuplicator dxgi;
        auto init = dxgi.Initialize(config.adapterIndex, config.outputIndex);
        if (!init) {
            remote::Logf(remote::LogLevel::Error, "SELFTEST dxgi FAIL init: {}", init.error());
            return 1;
        }
        for (uint64_t attempt = 1; attempt <= 30; ++attempt) {
            auto frame = dxgi.AcquireFrame(100, attempt);
            if (!frame) {
                remote::Logf(remote::LogLevel::Error, "SELFTEST dxgi FAIL acquire: {}", frame.error());
                return 1;
            }
            if (!frame.value().has_value()) {
                continue;
            }
            const auto& f = *frame.value();
            auto validation = remote::ValidateRawBGRAFrame(f.width, f.height, f.strideBytes, remote::AsBytes(f.bytes));
            auto saved = remote::SaveBGRA8ToBMP(artifacts / "dxgi_frame.bmp", f.width, f.height, f.strideBytes, remote::AsBytes(f.bytes));
            remote::Logf(validation.valid && saved ? remote::LogLevel::Info : remote::LogLevel::Error,
                         "SELFTEST dxgi {} {}x{} checksum={} uniqueColors={} save={}",
                         validation.valid ? "PASS" : "FAIL",
                         f.width,
                         f.height,
                         validation.checksum,
                         validation.uniqueSampledColors,
                         saved ? "ok" : saved.error());
            return validation.valid && saved ? 0 : 1;
        }
        remote::Log(remote::LogLevel::Error, "SELFTEST dxgi FAIL: all 30 acquire attempts timed out");
        return 1;
    }
    if (config.selfTest == "dxgi-resize") {
        remote::capture::DxgiDuplicator dxgi;
        auto init = dxgi.Initialize(config.adapterIndex, config.outputIndex);
        if (!init) {
            remote::Logf(remote::LogLevel::Error, "SELFTEST dxgi-resize FAIL init: {}", init.error());
            return 1;
        }
        const uint32_t targetW = config.width != 0 ? config.width : 640;
        const uint32_t targetH = config.height != 0 ? config.height : 360;
        for (uint64_t attempt = 1; attempt <= 30; ++attempt) {
            auto frame = dxgi.AcquireFrame(100, attempt);
            if (!frame) {
                remote::Logf(remote::LogLevel::Error, "SELFTEST dxgi-resize FAIL acquire: {}", frame.error());
                return 1;
            }
            if (!frame.value().has_value()) {
                continue;
            }
            const auto& f = *frame.value();
            auto resized = remote::ResizeBGRA8Nearest(remote::AsBytes(f.bytes), f.width, f.height, f.strideBytes, targetW, targetH);
            auto validation = remote::ValidateRawBGRAFrame(targetW, targetH, targetW * 4, remote::AsBytes(resized));
            auto saved = remote::SaveBGRA8ToBMP(artifacts / "dxgi_resized_frame.bmp", targetW, targetH, targetW * 4, remote::AsBytes(resized));
            remote::Logf(validation.valid && saved ? remote::LogLevel::Info : remote::LogLevel::Error,
                         "SELFTEST dxgi-resize {} source={}x{} target={}x{} checksum={} uniqueColors={} save={}",
                         validation.valid ? "PASS" : "FAIL",
                         f.width,
                         f.height,
                         targetW,
                         targetH,
                         validation.checksum,
                         validation.uniqueSampledColors,
                         saved ? "ok" : saved.error());
            return validation.valid && saved ? 0 : 1;
        }
        remote::Log(remote::LogLevel::Error, "SELFTEST dxgi-resize FAIL: all 30 acquire attempts timed out");
        return 1;
    }
    if (config.selfTest == "udp-send") {
        remote::transport::UdpVideoTransport udp;
        auto bound = udp.Bind("0.0.0.0", 0);
        if (!bound) {
            remote::Logf(remote::LogLevel::Error, "SELFTEST udp-send FAIL bind: {}", bound.error());
            return 1;
        }
        auto remoteResult = udp.SetRemote(config.udpTarget, config.udpPort);
        if (!remoteResult) {
            remote::Logf(remote::LogLevel::Error, "SELFTEST udp-send FAIL remote: {}", remoteResult.error());
            return 1;
        }
        const uint32_t width = config.width != 0 ? config.width : 640;
        const uint32_t height = config.height != 0 ? config.height : 360;
        const uint32_t fps = config.fps != 0 ? config.fps : 10;
        remote::encode::DummyRawFrameGenerator generator(width, height);
        uint64_t sequence = 1;
        uint64_t packets = 0;
        uint64_t bytes = 0;
        const uint64_t framesToSend = static_cast<uint64_t>(fps) * 3;
        auto nextFrameAt = std::chrono::steady_clock::now();
        const auto frameInterval = std::chrono::microseconds(1'000'000 / fps);
        for (uint64_t frameId = 1; frameId <= framesToSend; ++frameId) {
            nextFrameAt += frameInterval;
            auto frame = generator.Generate(frameId);
            auto sent = PacketizeAndSend(udp, frame.header, remote::AsBytes(frame.bytes), sequence, packets, bytes);
            if (!sent) {
                remote::Logf(remote::LogLevel::Error, "SELFTEST udp-send FAIL: {}", sent.error());
                return 1;
            }
            std::this_thread::sleep_until(nextFrameAt);
        }
        remote::Logf(remote::LogLevel::Info, "SELFTEST udp-send PASS frames={} packets={} bytes={}", framesToSend, packets, bytes);
        return 0;
    }
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    auto config = ParseArgs(argc, argv);
    if (config.listAdapters) {
        const auto listed = remote::capture::ListDxgiAdapters();
        return listed ? 0 : 1;
    }
    if (!config.selfTest.empty()) {
        const int result = RunSelfTest(config);
        if (result >= 0) {
            return result;
        }
    }
    remote::host::HostApp app(std::move(config));
    auto init = app.Initialize();
    if (!init) {
        remote::Logf(remote::LogLevel::Error, "Host initialization failed: {}", init.error());
        return 1;
    }
    return app.Run();
}
