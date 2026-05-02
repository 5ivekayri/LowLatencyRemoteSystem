#include "render/D3D11Renderer.h"

#include "common/Log.h"

#include <iterator>
#include <algorithm>
#include <utility>

namespace remote::render {

ResultT<D3D11DeviceContext> CreateD3D11Device() {
    D3D11DeviceContext out;
    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        levels,
        static_cast<UINT>(std::size(levels)),
        D3D11_SDK_VERSION,
        out.device.GetAddressOf(),
        &out.featureLevel,
        out.context.GetAddressOf());

#if defined(_DEBUG)
    if (FAILED(hr)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            out.device.GetAddressOf(),
            &out.featureLevel,
            out.context.GetAddressOf());
    }
#endif

    if (FAILED(hr)) {
        LogHResult("D3D11CreateDevice", hr);
        return ResultT<D3D11DeviceContext>::Fail("D3D11 device creation failed");
    }

    Logf(LogLevel::Info, "D3D11 device initialized, featureLevel=0x{:X}", static_cast<unsigned int>(out.featureLevel));
    return ResultT<D3D11DeviceContext>::Ok(std::move(out));
}

Result D3D11Renderer::Initialize() {
    auto d3d = CreateD3D11Device();
    if (!d3d) {
        return Result::Fail(d3d.error());
    }
    d3d_ = std::move(d3d).value();
    Log(LogLevel::Info, "D3D11Renderer initialized (stub)");
    return Result::Ok();
}

Result D3D11Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height) {
    hwnd_ = hwnd;
    width_ = width;
    height_ = height;

    auto d3d = CreateD3D11Device();
    if (!d3d) {
        return Result::Fail(d3d.error());
    }
    d3d_ = std::move(d3d).value();

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = d3d_.device.As(&dxgiDevice);
    if (FAILED(hr)) {
        LogHResult("ID3D11Device::QueryInterface(IDXGIDevice)", hr);
        return Result::Fail("query IDXGIDevice failed");
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) {
        LogHResult("IDXGIDevice::GetAdapter", hr);
        return Result::Fail("get DXGI adapter failed");
    }

    Microsoft::WRL::ComPtr<IDXGIFactory> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        LogHResult("IDXGIAdapter::GetParent(IDXGIFactory)", hr);
        return Result::Fail("get DXGI factory failed");
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = width_;
    desc.BufferDesc.Height = height_;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = hwnd_;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = factory->CreateSwapChain(d3d_.device.Get(), &desc, swapChain_.GetAddressOf());
    if (FAILED(hr)) {
        LogHResult("IDXGIFactory::CreateSwapChain", hr);
        return Result::Fail("create D3D11 swapchain failed");
    }

    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    auto rt = CreateRenderTarget();
    if (!rt) {
        return rt;
    }
    Logf(LogLevel::Info, "D3D11Renderer initialized for {}x{} Raw BGRA", width_, height_);
    return Result::Ok();
}

Result D3D11Renderer::Resize(uint32_t width, uint32_t height) {
    if (!swapChain_ || width == 0 || height == 0) {
        return Result::Ok();
    }
    renderTargetView_.Reset();
    width_ = width;
    height_ = height;
    const HRESULT hr = swapChain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        LogHResult("IDXGISwapChain::ResizeBuffers", hr);
        return Result::Fail("resize swapchain failed");
    }
    return CreateRenderTarget();
}

Result D3D11Renderer::RenderBGRA(const uint8_t* data, uint32_t width, uint32_t height, uint32_t stride) {
    if (!data || !swapChain_) {
        return Result::Fail("renderer is not initialized");
    }
    auto upload = EnsureUploadTexture(width, height);
    if (!upload) {
        return upload;
    }

    d3d_.context->UpdateSubresource(uploadTexture_.Get(), 0, nullptr, data, stride, 0);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        LogHResult("IDXGISwapChain::GetBuffer", hr);
        return Result::Fail("get swapchain backbuffer failed");
    }

    D3D11_BOX srcBox{};
    srcBox.left = 0;
    srcBox.top = 0;
    srcBox.front = 0;
    srcBox.right = std::min(width, width_);
    srcBox.bottom = std::min(height, height_);
    srcBox.back = 1;
    d3d_.context->CopySubresourceRegion(backBuffer.Get(), 0, 0, 0, 0, uploadTexture_.Get(), 0, &srcBox);
    return Result::Ok();
}

Result D3D11Renderer::Present() {
    if (!swapChain_) {
        return Result::Fail("swapchain is not initialized");
    }
    const HRESULT hr = swapChain_->Present(1, 0);
    if (FAILED(hr)) {
        LogHResult("IDXGISwapChain::Present", hr);
        return Result::Fail("present failed");
    }
    return Result::Ok();
}

void D3D11Renderer::Shutdown() {
    renderTargetView_.Reset();
    uploadTexture_.Reset();
    swapChain_.Reset();
    d3d_.context.Reset();
    d3d_.device.Reset();
    hwnd_ = nullptr;
    width_ = 0;
    height_ = 0;
}

Result D3D11Renderer::EnsureUploadTexture(uint32_t width, uint32_t height) {
    if (uploadTexture_) {
        D3D11_TEXTURE2D_DESC existing{};
        uploadTexture_->GetDesc(&existing);
        if (existing.Width == width && existing.Height == height) {
            return Result::Ok();
        }
        uploadTexture_.Reset();
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = 0;

    const HRESULT hr = d3d_.device->CreateTexture2D(&desc, nullptr, uploadTexture_.GetAddressOf());
    if (FAILED(hr)) {
        LogHResult("ID3D11Device::CreateTexture2D(upload)", hr);
        return Result::Fail("create upload texture failed");
    }
    return Result::Ok();
}

Result D3D11Renderer::CreateRenderTarget() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        LogHResult("IDXGISwapChain::GetBuffer(render target)", hr);
        return Result::Fail("get render target backbuffer failed");
    }
    hr = d3d_.device->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTargetView_.GetAddressOf());
    if (FAILED(hr)) {
        LogHResult("ID3D11Device::CreateRenderTargetView", hr);
        return Result::Fail("create render target view failed");
    }
    return Result::Ok();
}

Result D3D11Renderer::PresentStub() {
    // TODO: Create a Win32 window, swap chain, shaders, and upload/present decoded NV12/BGRA frames.
    Log(LogLevel::Debug, "D3D11Renderer present stub");
    return Result::Ok();
}

} // namespace remote::render
