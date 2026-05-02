#include "capture/DxgiAdapterEnumerator.h"

#include "common/Log.h"

#include <dxgi1_6.h>
#include <format>
#include <iostream>
#include <string>
#include <wrl/client.h>

namespace remote::capture {

namespace {

std::string WideToUtf8(const wchar_t* text) {
    if (!text) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

const char* RotationName(DXGI_MODE_ROTATION rotation) {
    switch (rotation) {
    case DXGI_MODE_ROTATION_IDENTITY: return "Identity";
    case DXGI_MODE_ROTATION_ROTATE90: return "Rotate90";
    case DXGI_MODE_ROTATION_ROTATE180: return "Rotate180";
    case DXGI_MODE_ROTATION_ROTATE270: return "Rotate270";
    default: return "Unspecified";
    }
}

} // namespace

Result ListDxgiAdapters() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        LogHResult("CreateDXGIFactory1", hr);
        return Result::Fail("create DXGI factory failed");
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(adapterIndex, adapter.GetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            LogHResult("IDXGIFactory1::EnumAdapters1", hr);
            return Result::Fail("enumerate DXGI adapters failed");
        }

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        std::cout << "Adapter " << adapterIndex << ":\n";
        std::cout << "  Name: " << WideToUtf8(desc.Description) << "\n";
        std::cout << std::format("  VendorId: 0x{:04X}\n", desc.VendorId);
        std::cout << std::format("  DeviceId: 0x{:04X}\n", desc.DeviceId);
        std::cout << "  DedicatedVideoMemory: " << desc.DedicatedVideoMemory << "\n";
        std::cout << "  DedicatedSystemMemory: " << desc.DedicatedSystemMemory << "\n";
        std::cout << "  SharedSystemMemory: " << desc.SharedSystemMemory << "\n";
        std::cout << "  SoftwareAdapter: " << ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? "true" : "false") << "\n";
        std::cout << "  Outputs:\n";

        bool anyOutput = false;
        for (UINT outputIndex = 0;; ++outputIndex) {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, output.GetAddressOf());
            if (hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(hr)) {
                LogHResult("IDXGIAdapter::EnumOutputs", hr);
                return Result::Fail("enumerate DXGI outputs failed");
            }

            anyOutput = true;
            DXGI_OUTPUT_DESC outputDesc{};
            output->GetDesc(&outputDesc);
            const LONG width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
            const LONG height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
            std::cout << "    Output " << outputIndex << ":\n";
            std::cout << "      Name: " << WideToUtf8(outputDesc.DeviceName) << "\n";
            std::cout << "      Desktop: " << outputDesc.DesktopCoordinates.left << "," << outputDesc.DesktopCoordinates.top
                      << " " << width << "x" << height << "\n";
            std::cout << "      Attached: " << (outputDesc.AttachedToDesktop ? "true" : "false") << "\n";
            std::cout << "      Rotation: " << RotationName(outputDesc.Rotation) << "\n";
        }

        if (!anyOutput) {
            std::cout << "    none\n";
        }
    }

    return Result::Ok();
}

} // namespace remote::capture
