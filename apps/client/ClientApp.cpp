#include "ClientApp.h"

#include "common/Clock.h"
#include "common/Log.h"
#include "protocol/Serializer.h"

#include <atomic>
#include <chrono>
#include <conio.h>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windows.h>

namespace remote::client {

namespace {

struct CompleteVideoFrame {
    protocol::VideoFrameHeader header;
    ByteBuffer payload;
};

struct RenderWindowState {
    render::D3D11Renderer* renderer = nullptr;
    std::atomic_bool* running = nullptr;
};

LRESULT CALLBACK ClientWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }

    auto* state = reinterpret_cast<RenderWindowState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    switch (message) {
    case WM_SIZE:
        if (state && state->renderer && wParam != SIZE_MINIMIZED) {
            const auto width = static_cast<uint32_t>(LOWORD(lParam));
            const auto height = static_cast<uint32_t>(HIWORD(lParam));
            state->renderer->Resize(width, height);
        }
        return 0;
    case WM_CLOSE:
        if (state && state->running) {
            *state->running = false;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state && state->running) {
            *state->running = false;
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, message, wParam, lParam);
    }
}

ResultT<CompleteVideoFrame> BuildCompleteFrame(uint64_t frameId, std::vector<ByteBuffer>& fragments) {
    ByteBuffer combined;
    size_t totalSize = 0;
    for (const auto& fragment : fragments) {
        totalSize += fragment.size();
    }
    if (totalSize < sizeof(protocol::VideoFrameHeader)) {
        return ResultT<CompleteVideoFrame>::Fail("complete frame is smaller than VideoFrameHeader");
    }
    combined.reserve(totalSize);
    for (const auto& fragment : fragments) {
        combined.insert(combined.end(), fragment.begin(), fragment.end());
    }

    CompleteVideoFrame frame;
    size_t offset = 0;
    if (!protocol::ReadPod(AsBytes(combined), offset, frame.header)) {
        return ResultT<CompleteVideoFrame>::Fail("failed to parse VideoFrameHeader");
    }
    if (frame.header.headerSize != sizeof(protocol::VideoFrameHeader)) {
        return ResultT<CompleteVideoFrame>::Fail("unsupported VideoFrameHeader size");
    }
    if (frame.header.frameId != frameId) {
        return ResultT<CompleteVideoFrame>::Fail("VideoFrameHeader frameId mismatch");
    }
    if (frame.header.payloadSizeBytes != combined.size() - sizeof(protocol::VideoFrameHeader)) {
        return ResultT<CompleteVideoFrame>::Fail("VideoFrameHeader payload size mismatch");
    }
    if (frame.header.payloadFormat != static_cast<uint32_t>(protocol::VideoPayloadFormat::RawBGRA8)) {
        return ResultT<CompleteVideoFrame>::Fail("unsupported video payload format");
    }
    if (frame.header.strideBytes < frame.header.width * 4) {
        return ResultT<CompleteVideoFrame>::Fail("invalid BGRA stride");
    }

    frame.payload.resize(frame.header.payloadSizeBytes);
    std::memcpy(frame.payload.data(), combined.data() + sizeof(protocol::VideoFrameHeader), frame.payload.size());
    return ResultT<CompleteVideoFrame>::Ok(std::move(frame));
}

} // namespace

ClientApp::ClientApp(ClientConfig config) : config_(std::move(config)) {}

Result ClientApp::Initialize() {
    if (config_.token.empty()) {
        return Result::Fail("client requires a non-empty pairing token");
    }

    Log(LogLevel::Info, "Video/decode/render/input initialization is intentionally disabled until TCP handshake succeeds");
    return Result::Ok();
}

int ClientApp::Run() {
    Logf(LogLevel::Info, "remote_client skeleton targeting {} TCP={} UDP={}", config_.host, config_.tcpPort, config_.udpPort);
    const auto handshake = RunHandshake();
    if (!handshake) {
        Logf(LogLevel::Error, "Handshake failed: {}", handshake.error());
        Log(LogLevel::Info, "Press Enter to stop remote_client skeleton");
        std::cin.get();
        return 1;
    }
    const auto receive = RunDummyVideoReceiveLoop();
    if (!receive) {
        Logf(LogLevel::Error, "Dummy UDP receive loop failed: {}", receive.error());
        return 1;
    }
    return 0;
}

Result ClientApp::RunHandshake() {
    auto connected = control_.Connect(config_.host, config_.tcpPort);
    if (!connected) {
        return connected;
    }

    protocol::Hello hello;
    hello.protocolVersion = protocol::Version;
    hello.clientName = "remote_client";
    hello.token = config_.token;
    hello.udpPort = config_.udpPort;

    auto helloPayload = protocol::SerializeHello(hello);
    auto sent = control_.SendMessage(protocol::MessageType::Hello, AsBytes(helloPayload));
    if (!sent) {
        return sent;
    }
    Logf(LogLevel::Info, "Sent Hello: protocolVersion={} clientName='{}'", hello.protocolVersion, hello.clientName);

    auto received = control_.ReceiveMessage();
    if (!received) {
        return Result::Fail(received.error());
    }
    if (static_cast<protocol::MessageType>(received.value().header.type) != protocol::MessageType::HostInfo) {
        return Result::Fail("expected HostInfo message");
    }

    auto info = protocol::DeserializeHostInfo(AsBytes(received.value().payload));
    if (!info) {
        return Result::Fail(info.error());
    }

    Logf(LogLevel::Info,
         "Received HostInfo: hostName='{}' {}x{}@{}Hz",
         info.value().hostName,
         info.value().width,
         info.value().height,
         info.value().refreshHz);
    if (info.value().width == 0 || info.value().height == 0) {
        Log(LogLevel::Error, "FATAL invalid HostInfo dimensions 0x0; refusing renderer fallback");
        return Result::Fail("invalid HostInfo dimensions");
    }
    if (info.value().width != 0 && info.value().height != 0) {
        streamWidth_ = info.value().width;
        streamHeight_ = info.value().height;
    }
    return Result::Ok();
}

Result ClientApp::RunDummyVideoReceiveLoop() {
    Logf(LogLevel::Info, "Client UDP bind {}:{}", config_.bindAddress, config_.udpPort);
    auto udp = video_.Bind(config_.bindAddress, config_.udpPort);
    if (!udp) {
        return udp;
    }

    struct PendingFrame {
        uint16_t fragmentCount = 0;
        uint16_t receivedCount = 0;
        size_t totalBytes = 0;
        std::vector<ByteBuffer> fragments;
        std::chrono::steady_clock::time_point firstSeen;
    };

    std::mutex latestMutex;
    std::optional<CompleteVideoFrame> latestFrame;
    std::unordered_map<uint64_t, PendingFrame> pending;
    std::optional<uint64_t> highestSequence;
    uint32_t loggedPackets = 0;
    bool loggedCompleteFrame = false;
    bool loggedNoUdp = false;
    const auto handshakeDoneAt = std::chrono::steady_clock::now();
    std::atomic_uint64_t droppedRenderFrames = 0;
    std::atomic_uint64_t lastFrameId = 0;
    std::atomic_bool loggedFirstFrame = false;

    std::atomic_bool running = true;
    std::thread inputThread([&running]() {
        Log(LogLevel::Info, "Dummy UDP receive running. Press Enter to stop remote_client");
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

    std::thread networkThread([&]() {
        uint64_t packetsReceivedThisSecond = 0;
        uint64_t framesReceivedThisSecond = 0;
        uint64_t packetsLostTotal = 0;
        uint64_t framesDroppedTotal = 0;
        auto nextStatsAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        constexpr auto frameTimeout = std::chrono::milliseconds(250);

        while (running) {
            auto packet = video_.ReceivePacket();
            const auto now = std::chrono::steady_clock::now();
            if (packet) {
                const auto& header = packet.value().header;
                if (loggedPackets < 3) {
                    Logf(LogLevel::Info,
                         "Received UDP packet: sequence={} frameId={} fragment={}/{} payloadSize={}",
                         header.sequence,
                         header.frameId,
                         header.fragmentIndex,
                         header.fragmentCount,
                         header.payloadSize);
                    ++loggedPackets;
                }
                if (!highestSequence.has_value()) {
                    highestSequence = header.sequence;
                } else if (header.sequence > *highestSequence + 1) {
                    packetsLostTotal += header.sequence - *highestSequence - 1;
                    highestSequence = header.sequence;
                } else if (header.sequence > *highestSequence) {
                    highestSequence = header.sequence;
                }

                ++packetsReceivedThisSecond;

                if (header.fragmentCount == 0 || header.fragmentIndex >= header.fragmentCount) {
                    Log(LogLevel::Warn, "Dropping malformed UDP fragment metadata");
                } else {
                    auto& frame = pending[header.frameId];
                    if (frame.fragmentCount == 0) {
                        frame.fragmentCount = header.fragmentCount;
                        frame.fragments.resize(header.fragmentCount);
                        frame.firstSeen = now;
                    }

                    if (frame.fragmentCount == header.fragmentCount && frame.fragments[header.fragmentIndex].empty()) {
                        frame.totalBytes += packet.value().payload.size();
                        frame.fragments[header.fragmentIndex] = std::move(packet.value().payload);
                        ++frame.receivedCount;
                    }

                    if (frame.receivedCount == frame.fragmentCount) {
                        auto complete = BuildCompleteFrame(header.frameId, frame.fragments);
                        if (complete) {
                            if (!loggedCompleteFrame) {
                                loggedCompleteFrame = true;
                                Logf(LogLevel::Info,
                                     "First complete frame: frameId={} format={} {}x{} strideBytes={} payloadBytes={}",
                                     complete.value().header.frameId,
                                     complete.value().header.payloadFormat,
                                     complete.value().header.width,
                                     complete.value().header.height,
                                     complete.value().header.strideBytes,
                                     complete.value().payload.size());
                            }
                            if (!loggedFirstFrame.exchange(true)) {
                                Logf(LogLevel::Info,
                                     "First video frame: {}x{} stride={} format=RawBGRA8 payload={} bytes",
                                     complete.value().header.width,
                                     complete.value().header.height,
                                     complete.value().header.strideBytes,
                                     complete.value().payload.size());
                            }
                            lastFrameId = complete.value().header.frameId;
                            {
                                std::lock_guard lock(latestMutex);
                                if (latestFrame.has_value()) {
                                    ++droppedRenderFrames;
                                }
                                latestFrame = std::move(complete).value();
                            }
                            ++framesReceivedThisSecond;
                        } else {
                            Logf(LogLevel::Warn, "Dropping complete frame {}: {}", header.frameId, complete.error());
                            ++framesDroppedTotal;
                        }
                        pending.erase(header.frameId);
                    }
                }
            } else if (packet.error() != "UDP receive timeout") {
                Logf(LogLevel::Warn, "UDP receive warning: {}", packet.error());
            }
            if (!loggedNoUdp && packetsReceivedThisSecond == 0 && !highestSequence.has_value() && now - handshakeDoneAt > std::chrono::seconds(2)) {
                loggedNoUdp = true;
                Log(LogLevel::Error, "No UDP packets received. Check host UDP target, client UDP bind, firewall.");
            }

            for (auto it = pending.begin(); it != pending.end();) {
                if (now - it->second.firstSeen > frameTimeout) {
                    ++framesDroppedTotal;
                    it = pending.erase(it);
                } else {
                    ++it;
                }
            }

            if (now >= nextStatsAt) {
                Logf(LogLevel::Info,
                     "UDP receive stats: frames/s={} packets/s={} lostPacketsTotal={} droppedFramesTotal={} pendingFrames={}",
                     framesReceivedThisSecond,
                     packetsReceivedThisSecond,
                     packetsLostTotal,
                     framesDroppedTotal,
                     pending.size());
                framesReceivedThisSecond = 0;
                packetsReceivedThisSecond = 0;
                nextStatsAt = now + std::chrono::seconds(1);
            }
        }
    });

    WNDCLASSA wc{};
    wc.lpfnWndProc = ClientWindowProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "RemoteClientRawBgraWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);

    RECT rect{0, 0, static_cast<LONG>(streamWidth_), static_cast<LONG>(streamHeight_)};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    RenderWindowState windowState{&renderer_, &running};
    HWND hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "remote_client - Raw BGRA stream",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        wc.hInstance,
        &windowState);
    if (!hwnd) {
        running = false;
        if (networkThread.joinable()) {
            networkThread.join();
        }
        if (inputThread.joinable()) {
            inputThread.join();
        }
        LogWin32Error("CreateWindowExA", GetLastError());
        return Result::Fail("create client render window failed");
    }

    auto renderer = renderer_.Initialize(hwnd, streamWidth_, streamHeight_);
    if (!renderer) {
        running = false;
        DestroyWindow(hwnd);
        if (networkThread.joinable()) {
            networkThread.join();
        }
        if (inputThread.joinable()) {
            inputThread.join();
        }
        return renderer;
    }

    MSG msg{};
    uint64_t renderedFramesThisSecond = 0;
    uint32_t renderWidth = streamWidth_;
    uint32_t renderHeight = streamHeight_;
    auto nextRenderStatsAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);

    while (running) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        std::optional<CompleteVideoFrame> frameToRender;
        {
            std::lock_guard lock(latestMutex);
            if (latestFrame.has_value()) {
                frameToRender = std::move(*latestFrame);
                latestFrame.reset();
            }
        }

        if (frameToRender) {
            if (frameToRender->header.width != renderWidth || frameToRender->header.height != renderHeight) {
                renderWidth = frameToRender->header.width;
                renderHeight = frameToRender->header.height;
                RECT resizeRect{0, 0, static_cast<LONG>(renderWidth), static_cast<LONG>(renderHeight)};
                AdjustWindowRect(&resizeRect, WS_OVERLAPPEDWINDOW, FALSE);
                SetWindowPos(
                    hwnd,
                    nullptr,
                    0,
                    0,
                    resizeRect.right - resizeRect.left,
                    resizeRect.bottom - resizeRect.top,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                renderer_.Resize(renderWidth, renderHeight);
                Logf(LogLevel::Info, "Renderer resized for incoming frame: {}x{}", renderWidth, renderHeight);
            }
            auto render = renderer_.RenderBGRA(
                reinterpret_cast<const uint8_t*>(frameToRender->payload.data()),
                frameToRender->header.width,
                frameToRender->header.height,
                frameToRender->header.strideBytes);
            if (render) {
                auto present = renderer_.Present();
                if (!present) {
                    Logf(LogLevel::Warn, "Present failed: {}", present.error());
                } else {
                    ++renderedFramesThisSecond;
                }
            } else {
                Logf(LogLevel::Warn, "RenderBGRA failed: {}", render.error());
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextRenderStatsAt) {
            Logf(LogLevel::Info,
                 "Render stats: renderedFrames/s={} droppedRenderFrames={} lastFrameId={}",
                 renderedFramesThisSecond,
                 droppedRenderFrames.load(),
                 lastFrameId.load());
            renderedFramesThisSecond = 0;
            nextRenderStatsAt = now + std::chrono::seconds(1);
        }
    }

    renderer_.Shutdown();
    if (inputThread.joinable()) {
        inputThread.join();
    }
    if (networkThread.joinable()) {
        networkThread.join();
    }
    return Result::Ok();
}

} // namespace remote::client
