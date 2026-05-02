#include "capture/DxgiDuplicator.h"

#include "common/Clock.h"
#include "common/Log.h"

#include <cstring>
#include <dxgi1_6.h>
#include <utility>

namespace remote::capture {

Result DxgiDuplicator::Initialize() {
    return Initialize(0, 0);
}

Result DxgiDuplicator::Initialize(uint32_t adapterIndex, uint32_t outputIndex) {
    Shutdown();
    adapterIndex_ = adapterIndex;
    outputIndex_ = outputIndex;

    if (GetSystemMetrics(SM_REMOTESESSION) != 0) {
        Log(LogLevel::Warn, "Windows reports a remote session; DXGI Desktop Duplication may be unavailable under RDP/remote console");
    }

    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        LogHResult("CreateDXGIFactory1", hr);
        return Result::Fail("create DXGI factory failed");
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
    hr = factory->EnumAdapters1(adapterIndex_, adapter1.GetAddressOf());
    if (hr == DXGI_ERROR_NOT_FOUND) {
        Logf(LogLevel::Error, "DXGI adapter {} was not found", adapterIndex_);
        return Result::Fail("selected DXGI adapter was not found");
    }
    if (FAILED(hr)) {
        LogHResult("IDXGIFactory1::EnumAdapters1", hr);
        return Result::Fail("enumerate selected DXGI adapter failed");
    }

    DXGI_ADAPTER_DESC1 adapterDesc{};
    adapter1->GetDesc1(&adapterDesc);

    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    hr = D3D11CreateDevice(
        adapter1.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        flags,
        levels,
        static_cast<UINT>(std::size(levels)),
        D3D11_SDK_VERSION,
        d3d_.device.GetAddressOf(),
        &d3d_.featureLevel,
        d3d_.context.GetAddressOf());
#if defined(_DEBUG)
    if (FAILED(hr)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            adapter1.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            d3d_.device.GetAddressOf(),
            &d3d_.featureLevel,
            d3d_.context.GetAddressOf());
    }
#endif
    if (FAILED(hr)) {
        LogHResult("D3D11CreateDevice(selected adapter)", hr);
        return Result::Fail("create D3D11 device on selected adapter failed");
    }

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    hr = adapter1->EnumOutputs(outputIndex_, output.GetAddressOf());
    if (hr == DXGI_ERROR_NOT_FOUND) {
        Logf(LogLevel::Error, "DXGI output {} was not found on adapter {}", outputIndex_, adapterIndex_);
        return Result::Fail("selected DXGI output was not found");
    }
    if (FAILED(hr)) {
        LogHResult("IDXGIAdapter::EnumOutputs", hr);
        return Result::Fail("enumerate requested DXGI output failed");
    }

    DXGI_OUTPUT_DESC outputDesc{};
    hr = output->GetDesc(&outputDesc);
    if (FAILED(hr)) {
        LogHResult("IDXGIOutput::GetDesc", hr);
        return Result::Fail("get DXGI output description failed");
    }

    hr = output.As(&output_);
    if (FAILED(hr)) {
        LogHResult("IDXGIOutput::QueryInterface(IDXGIOutput1)", hr);
        return Result::Fail("query IDXGIOutput1 failed");
    }

    char adapterName[128]{};
    WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, adapterName, static_cast<int>(sizeof(adapterName)), nullptr, nullptr);
    char deviceName[32]{};
    WideCharToMultiByte(CP_UTF8, 0, outputDesc.DeviceName, -1, deviceName, static_cast<int>(sizeof(deviceName)), nullptr, nullptr);
    Logf(LogLevel::Info,
         "DXGI selected adapter={} '{}' output={} '{}' before DuplicateOutput",
         adapterIndex_,
         adapterName,
         outputIndex_,
         deviceName);

    hr = output_->DuplicateOutput(d3d_.device.Get(), duplication_.GetAddressOf());
    if (FAILED(hr)) {
        LogHResult("IDXGIOutput1::DuplicateOutput", hr);
        Logf(LogLevel::Error, "DuplicateOutput failed for adapter={} output={}", adapterIndex_, outputIndex_);
        if (hr == DXGI_ERROR_UNSUPPORTED) {
            Log(LogLevel::Warn, "Desktop Duplication is unsupported for this output/session. Common causes: RDP session, disabled/virtual display, old or virtual GPU driver, or an output mode unsupported by DXGI duplication.");
        }
        return Result::Fail("DXGI output duplication initialization failed");
    }

    DXGI_OUTDUPL_DESC duplicationDesc{};
    duplication_->GetDesc(&duplicationDesc);
    width_ = duplicationDesc.ModeDesc.Width;
    height_ = duplicationDesc.ModeDesc.Height;
    format_ = duplicationDesc.ModeDesc.Format;

    Logf(LogLevel::Info,
         "DXGI desktop duplication initialized: adapter={} output={} device='{}' resolution={}x{} format={}",
         adapterIndex_,
         outputIndex_,
         deviceName,
         width_,
         height_,
         static_cast<uint32_t>(format_));
    if (format_ != DXGI_FORMAT_B8G8R8A8_UNORM) {
        Log(LogLevel::Warn, "DXGI output format is not B8G8R8A8_UNORM; TODO: add GPU color conversion path if needed");
    }
    return Result::Ok();
}

ResultT<std::optional<CapturedRawFrame>> DxgiDuplicator::AcquireFrame(uint32_t timeoutMs, uint64_t frameIdForLog) {
    if (!duplication_) {
        return ResultT<std::optional<CapturedRawFrame>>::Fail("DXGI duplicator is not initialized");
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    Microsoft::WRL::ComPtr<IDXGIResource> resource;
    const HRESULT acquireHr = duplication_->AcquireNextFrame(timeoutMs, &frameInfo, resource.GetAddressOf());
    if (acquireHr == DXGI_ERROR_WAIT_TIMEOUT) {
        return ResultT<std::optional<CapturedRawFrame>>::Ok(std::nullopt);
    }
    if (acquireHr == DXGI_ERROR_ACCESS_LOST) {
        LogHResult("IDXGIOutputDuplication::AcquireNextFrame", acquireHr);
        return ResultT<std::optional<CapturedRawFrame>>::Fail("DXGI access lost; TODO: reinitialize duplication after display mode/session change");
    }
    if (FAILED(acquireHr)) {
        LogHResult("IDXGIOutputDuplication::AcquireNextFrame", acquireHr);
        return ResultT<std::optional<CapturedRawFrame>>::Fail("acquire DXGI frame failed");
    }

    struct FrameReleaseGuard {
        IDXGIOutputDuplication* duplication = nullptr;
        ~FrameReleaseGuard() {
            if (duplication) {
                duplication->ReleaseFrame();
            }
        }
    } releaseGuard{duplication_.Get()};

    Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
    HRESULT hr = resource.As(&desktopTexture);
    if (FAILED(hr)) {
        LogHResult("IDXGIResource::QueryInterface(ID3D11Texture2D)", hr);
        return ResultT<std::optional<CapturedRawFrame>>::Fail("query desktop texture failed");
    }

    D3D11_TEXTURE2D_DESC desktopDesc{};
    desktopTexture->GetDesc(&desktopDesc);
    bool recreateStaging = !staging_;
    if (staging_) {
        D3D11_TEXTURE2D_DESC currentStaging{};
        staging_->GetDesc(&currentStaging);
        recreateStaging = currentStaging.Width != desktopDesc.Width || currentStaging.Height != desktopDesc.Height || currentStaging.Format != desktopDesc.Format;
    }
    if (recreateStaging) {
        staging_.Reset();
        D3D11_TEXTURE2D_DESC stagingDesc = desktopDesc;
        stagingDesc.BindFlags = 0;
        stagingDesc.MiscFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        hr = d3d_.device->CreateTexture2D(&stagingDesc, nullptr, staging_.GetAddressOf());
        if (FAILED(hr)) {
            LogHResult("ID3D11Device::CreateTexture2D(staging)", hr);
            return ResultT<std::optional<CapturedRawFrame>>::Fail("create DXGI staging texture failed");
        }
    }

    d3d_.context->CopyResource(staging_.Get(), desktopTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = d3d_.context->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        LogHResult("ID3D11DeviceContext::Map(staging)", hr);
        return ResultT<std::optional<CapturedRawFrame>>::Fail("map DXGI staging texture failed");
    }

    CapturedRawFrame frame;
    frame.timestampUs = NowUs();
    frame.width = desktopDesc.Width;
    frame.height = desktopDesc.Height;
    frame.strideBytes = frame.width * 4;
    frame.bytes.resize(static_cast<size_t>(frame.strideBytes) * frame.height);

    const auto* src = static_cast<const std::byte*>(mapped.pData);
    for (uint32_t y = 0; y < frame.height; ++y) {
        const auto* srcRow = src + static_cast<size_t>(y) * mapped.RowPitch;
        auto* dstRow = frame.bytes.data() + static_cast<size_t>(y) * frame.strideBytes;
        std::memcpy(dstRow, srcRow, frame.strideBytes);
    }

    d3d_.context->Unmap(staging_.Get(), 0);
    if (!loggedFirstFrame_) {
        loggedFirstFrame_ = true;
        Logf(LogLevel::Info,
             "DXGI first captured frame: frameId={} width={} height={} strideBytes={} dataSize={}",
             frameIdForLog,
             frame.width,
             frame.height,
             frame.strideBytes,
             frame.bytes.size());
    }
    return ResultT<std::optional<CapturedRawFrame>>::Ok(std::move(frame));
}

void DxgiDuplicator::Shutdown() {
    staging_.Reset();
    duplication_.Reset();
    output_.Reset();
    d3d_.context.Reset();
    d3d_.device.Reset();
    width_ = 0;
    height_ = 0;
    format_ = DXGI_FORMAT_UNKNOWN;
    loggedFirstFrame_ = false;
}

Result DxgiDuplicator::CaptureNextFrameStub() {
    if (!duplication_) {
        return Result::Fail("DXGI duplicator is not initialized");
    }
    // TODO: Call AcquireNextFrame, copy the ID3D11Texture2D, record capture timestamps, and ReleaseFrame.
    Log(LogLevel::Debug, "DxgiDuplicator capture stub");
    return Result::Ok();
}

} // namespace remote::capture
