#include "ClientApp.h"

#include "common/Log.h"
#include "common/RawFrameUtils.h"
#include "encode/DummyRawFrameGenerator.h"
#include "protocol/Serializer.h"
#include "render/D3D11Renderer.h"
#include "transport/UdpVideoTransport.h"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windows.h>

namespace {

uint16_t ParsePort(std::string_view value, uint16_t fallback) {
    uint16_t port = fallback;
    std::from_chars(value.data(), value.data() + value.size(), port);
    return port;
}

remote::client::ClientConfig ParseArgs(int argc, char** argv) {
    remote::client::ClientConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            config.host = argv[++i];
        } else if (arg == "--self-test" && i + 1 < argc) {
            config.selfTest = argv[++i];
        } else if (arg == "--bind" && i + 1 < argc) {
            config.bindAddress = argv[++i];
        } else if (arg == "--token" && i + 1 < argc) {
            config.token = argv[++i];
        } else if (arg == "--tcp-port" && i + 1 < argc) {
            config.tcpPort = ParsePort(argv[++i], config.tcpPort);
        } else if (arg == "--tcp" && i + 1 < argc) {
            config.tcpPort = ParsePort(argv[++i], config.tcpPort);
        } else if (arg == "--udp-port" && i + 1 < argc) {
            config.udpPort = ParsePort(argv[++i], config.udpPort);
        } else if (arg == "--udp" && i + 1 < argc) {
            config.udpPort = ParsePort(argv[++i], config.udpPort);
        }
    }
    return config;
}

struct MiniFrame {
    remote::protocol::VideoFrameHeader header;
    remote::ByteBuffer payload;
};

std::optional<MiniFrame> TryBuildFrame(uint64_t frameId, std::vector<remote::ByteBuffer>& fragments) {
    remote::ByteBuffer combined;
    size_t total = 0;
    for (const auto& f : fragments) {
        total += f.size();
    }
    if (total < sizeof(remote::protocol::VideoFrameHeader)) {
        return std::nullopt;
    }
    combined.reserve(total);
    for (const auto& f : fragments) {
        combined.insert(combined.end(), f.begin(), f.end());
    }
    MiniFrame frame;
    size_t offset = 0;
    if (!remote::protocol::ReadPod(remote::AsBytes(combined), offset, frame.header) || frame.header.frameId != frameId) {
        return std::nullopt;
    }
    if (frame.header.payloadSizeBytes != combined.size() - sizeof(remote::protocol::VideoFrameHeader)) {
        return std::nullopt;
    }
    frame.payload.assign(combined.begin() + sizeof(remote::protocol::VideoFrameHeader), combined.end());
    return frame;
}

LRESULT CALLBACK TestWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int RunSelfTest(remote::client::ClientConfig& config) {
    if (config.selfTest == "udp-recv") {
        remote::transport::UdpVideoTransport udp;
        auto bound = udp.Bind(config.bindAddress, config.udpPort);
        if (!bound) {
            remote::Logf(remote::LogLevel::Error, "SELFTEST udp-recv FAIL bind: {}", bound.error());
            return 1;
        }
        struct Pending {
            uint16_t fragmentCount = 0;
            uint16_t receivedCount = 0;
            std::vector<remote::ByteBuffer> fragments;
        };
        std::unordered_map<uint64_t, Pending> pending;
        uint64_t packets = 0;
        uint64_t frames = 0;
        auto endAt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < endAt) {
            auto packet = udp.ReceivePacket();
            if (!packet) {
                continue;
            }
            ++packets;
            const auto& h = packet.value().header;
            auto& p = pending[h.frameId];
            if (p.fragmentCount == 0) {
                p.fragmentCount = h.fragmentCount;
                p.fragments.resize(h.fragmentCount);
            }
            if (h.fragmentIndex < p.fragments.size() && p.fragments[h.fragmentIndex].empty()) {
                p.fragments[h.fragmentIndex] = std::move(packet.value().payload);
                ++p.receivedCount;
            }
            if (p.receivedCount == p.fragmentCount) {
                auto frame = TryBuildFrame(h.frameId, p.fragments);
                if (frame) {
                    ++frames;
                    auto saved = remote::SaveBGRA8ToBMP("artifacts/udp_received_frame.bmp", frame->header.width, frame->header.height, frame->header.strideBytes, remote::AsBytes(frame->payload));
                    remote::Logf(saved ? remote::LogLevel::Info : remote::LogLevel::Error, "SELFTEST udp-recv {} packets={} frames={} save={}", saved ? "PASS" : "FAIL", packets, frames, saved ? "ok" : saved.error());
                    return saved ? 0 : 1;
                }
                pending.erase(h.frameId);
            }
        }
        remote::Logf(remote::LogLevel::Error, "SELFTEST udp-recv FAIL packets={} completeFrames=0", packets);
        return 1;
    }
    if (config.selfTest == "renderer") {
        constexpr uint32_t width = 640;
        constexpr uint32_t height = 360;
        WNDCLASSA wc{};
        wc.lpfnWndProc = TestWndProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "RemoteRendererSelfTest";
        RegisterClassA(&wc);
        HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "renderer self-test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd) {
            remote::LogWin32Error("CreateWindowExA(renderer self-test)", GetLastError());
            return 1;
        }
        remote::render::D3D11Renderer renderer;
        auto init = renderer.Initialize(hwnd, width, height);
        if (!init) {
            remote::Logf(remote::LogLevel::Error, "SELFTEST renderer FAIL init: {}", init.error());
            return 1;
        }
        remote::encode::DummyRawFrameGenerator generator(width, height);
        uint64_t rendered = 0;
        auto endAt = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        MSG msg{};
        while (std::chrono::steady_clock::now() < endAt) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            auto frame = generator.Generate(rendered + 1);
            auto r = renderer.RenderBGRA(reinterpret_cast<const uint8_t*>(frame.bytes.data()), frame.header.width, frame.header.height, frame.header.strideBytes);
            auto p = renderer.Present();
            if (!r || !p) {
                remote::Log(remote::LogLevel::Error, "SELFTEST renderer FAIL render/present");
                return 1;
            }
            ++rendered;
        }
        renderer.Shutdown();
        DestroyWindow(hwnd);
        remote::Logf(remote::LogLevel::Info, "SELFTEST renderer PASS renderedFrames={}", rendered);
        return rendered > 0 ? 0 : 1;
    }
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    auto config = ParseArgs(argc, argv);
    if (!config.selfTest.empty()) {
        const int result = RunSelfTest(config);
        if (result >= 0) {
            return result;
        }
    }
    remote::client::ClientApp app(std::move(config));
    auto init = app.Initialize();
    if (!init) {
        remote::Logf(remote::LogLevel::Error, "Client initialization failed: {}", init.error());
        return 1;
    }
    return app.Run();
}
