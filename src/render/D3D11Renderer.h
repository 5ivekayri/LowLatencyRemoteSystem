#pragma once

#include "common/Result.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdint>
#include <windows.h>
#include <wrl/client.h>

namespace remote::render {

struct D3D11DeviceContext {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
};

ResultT<D3D11DeviceContext> CreateD3D11Device();

class D3D11Renderer {
public:
    Result Initialize();
    Result Initialize(HWND hwnd, uint32_t width, uint32_t height);
    Result Resize(uint32_t width, uint32_t height);
    Result RenderBGRA(const uint8_t* data, uint32_t width, uint32_t height, uint32_t stride);
    Result Present();
    void Shutdown();
    Result PresentStub();

private:
    Result EnsureUploadTexture(uint32_t width, uint32_t height);
    Result CreateRenderTarget();

    D3D11DeviceContext d3d_;
    HWND hwnd_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> uploadTexture_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView_;
};

} // namespace remote::render
