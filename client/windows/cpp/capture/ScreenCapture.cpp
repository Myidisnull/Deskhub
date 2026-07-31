#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#include "capture/ScreenCapture.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <inspectable.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <atomic>
#include <cstdio>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace wg = winrt::Windows::Graphics;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgdx = winrt::Windows::Graphics::DirectX;
namespace wgd3 = winrt::Windows::Graphics::DirectX::Direct3D11;
using winrt::Windows::Foundation::Metadata::ApiInformation;

namespace capture {
void InitRuntime() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
}
}

template <typename T>
static winrt::com_ptr<T> GetDXGIInterface(winrt::Windows::Foundation::IInspectable const& obj) {
    auto access = obj.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<T> result;
    winrt::check_hresult(access->GetInterface(winrt::guid_of<T>(), result.put_void()));
    return result;
}

struct ScreenCapture::Impl {
    winrt::com_ptr<ID3D11Device> d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;

    wgd3::IDirect3DDevice winrtDevice{nullptr};
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool framePool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    winrt::event_token closedToken{};
    winrt::event_token frameArrivedToken{};
    wg::SizeInt32 lastSize{};

    FrameHandler onFrame;
    std::atomic<bool> closed{false};
    std::atomic<uint64_t> frameId{0};

    bool CreateDevice() {
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            d3dDevice.put(), nullptr, d3dContext.put());
        if (FAILED(hr)) {
            std::printf("D3D11CreateDevice failed: 0x%08lX\n", (unsigned long)hr);
            return false;
        }
        return true;
    }

    void OnFrameArrived() {
        if (!framePool) return;

        while (auto frame = framePool.TryGetNextFrame()) {
            auto size = frame.ContentSize();

            if (size.Width != lastSize.Width || size.Height != lastSize.Height) {
                std::printf("Window size changed: %dx%d\n", size.Width, size.Height);
                lastSize = size;
                framePool.Recreate(
                    winrtDevice, wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
                continue;
            }

            auto tex = GetDXGIInterface<ID3D11Texture2D>(frame.Surface());
            if (!tex) continue;

            if (onFrame) {
                FrameInfo info{};
                info.texture = tex.get();
                info.meta.width = static_cast<uint32_t>(size.Width);
                info.meta.height = static_cast<uint32_t>(size.Height);
                info.meta.timestampUs =
                    static_cast<uint64_t>(frame.SystemRelativeTime().count()) / 10ull;
                info.meta.frameId = frameId.fetch_add(1);
                onFrame(info);
            }
        }
    }
};

ScreenCapture::ScreenCapture() : impl_(std::make_unique<Impl>()) {}

ScreenCapture::~ScreenCapture() {
    Stop();
}

void ScreenCapture::SetDevice(ID3D11Device* device) {
    pendingDevice_ = device;
}

bool ScreenCapture::Start(uint64_t targetId, const deskhub::media::CaptureOptions& opt,
    FrameHandler onFrame) {
    (void)opt;
    HMONITOR monitor = (HMONITOR)(uintptr_t)targetId;
    ID3D11Device* device = pendingDevice_;
    if (!wgc::GraphicsCaptureSession::IsSupported()) {
        std::printf("Windows Graphics Capture is not supported on this machine.\n");
        return false;
    }
    if (!monitor) {
        std::printf("ScreenCapture::Start needs a monitor handle.\n");
        return false;
    }

    impl_->onFrame = std::move(onFrame);

    if (device) {
        impl_->d3dDevice.copy_from(device);
        impl_->d3dDevice->GetImmediateContext(impl_->d3dContext.put());
    } else if (!impl_->CreateDevice()) {
        return false;
    }

    auto dxgiDevice = impl_->d3dDevice.as<IDXGIDevice>();
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(
        CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    impl_->winrtDevice = inspectable.as<wgd3::IDirect3DDevice>();

    auto factory = winrt::get_activation_factory<wgc::GraphicsCaptureItem>();
    auto interop = factory.as<IGraphicsCaptureItemInterop>();
    const HRESULT hr = interop->CreateForMonitor(monitor,
        winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(impl_->item));
    if (FAILED(hr)) {
        std::printf("CreateForMonitor failed: 0x%08lX\n", (unsigned long)hr);
        return false;
    }

    impl_->lastSize = impl_->item.Size();
    std::printf("Capture source size: %dx%d\n", impl_->lastSize.Width, impl_->lastSize.Height);

    impl_->framePool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
        impl_->winrtDevice, wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, impl_->lastSize);

    impl_->session = impl_->framePool.CreateCaptureSession(impl_->item);

    if (ApiInformation::IsPropertyPresent(
            L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled")) {
        impl_->session.IsCursorCaptureEnabled(false);
    }
    if (ApiInformation::IsPropertyPresent(
            L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
        try {
            impl_->session.IsBorderRequired(false);
        } catch (winrt::hresult_error const&) {
        }
    }

    impl_->closedToken = impl_->item.Closed(
        [impl = impl_.get()](auto&&, auto&&) { impl->closed = true; });
    impl_->frameArrivedToken = impl_->framePool.FrameArrived(
        [impl = impl_.get()](auto&&, auto&&) { impl->OnFrameArrived(); });

    impl_->session.StartCapture();
    std::printf("Started capturing (via FrameArrived event).\n");
    return true;
}

void ScreenCapture::Stop() {
    if (!impl_) return;
    if (impl_->framePool && impl_->frameArrivedToken.value) {
        impl_->framePool.FrameArrived(impl_->frameArrivedToken);
        impl_->frameArrivedToken = {};
    }
    if (impl_->item && impl_->closedToken.value) {
        impl_->item.Closed(impl_->closedToken);
        impl_->closedToken = {};
    }
    impl_->session = nullptr;
    impl_->framePool = nullptr;
    impl_->item = nullptr;
    impl_->winrtDevice = nullptr;
    impl_->d3dContext = nullptr;
    impl_->d3dDevice = nullptr;
    impl_->onFrame = nullptr;
}

bool ScreenCapture::Closed() const {
    return impl_->closed;
}

ID3D11Device* ScreenCapture::Device() const {
    return impl_->d3dDevice.get();
}
ID3D11DeviceContext* ScreenCapture::Context() const {
    return impl_->d3dContext.get();
}
