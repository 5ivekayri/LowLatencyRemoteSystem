#pragma once

#include "capture/ICaptureSource.h"
#include "common/ByteBuffer.h"
#include "render/D3D11Renderer.h"

#include <cstdint>
#include <dxgi1_2.h>
#include <optional>
#include <wrl/client.h>

namespace remote::capture {

struct CapturedRawFrame {
    uint64_t timestampUs = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t strideBytes = 0;
    ByteBuffer bytes;
};

class DxgiDuplicator final : public ICaptureSource {
public:
    Result Initialize() override;
    Result Initialize(uint32_t adapterIndex, uint32_t outputIndex);
    ResultT<std::optional<CapturedRawFrame>> AcquireFrame(uint32_t timeoutMs, uint64_t frameIdForLog);
    void Shutdown();
    Result CaptureNextFrameStub() override;
    [[nodiscard]] uint32_t width() const noexcept { return width_; }
    [[nodiscard]] uint32_t height() const noexcept { return height_; }
    [[nodiscard]] DXGI_FORMAT format() const noexcept { return format_; }

private:
    render::D3D11DeviceContext d3d_;
    uint32_t adapterIndex_ = 0;
    uint32_t outputIndex_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    bool loggedFirstFrame_ = false;
    Microsoft::WRL::ComPtr<IDXGIOutput1> output_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
};

} // namespace remote::capture
