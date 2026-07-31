#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "DeskhubApi.h"

#include <windows.h>
#include <objbase.h>
#include <wrl/client.h>

#include <atomic>
#include <memory>

#include "capture/GpuSelect.h"
#include "decode/PanelRenderer.h"
#include "decode/WinVideoDecoder.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/ClientEngine.h"
#include "deskhubp/system/Clock.h"

namespace {

constexpr const char* kStatusSeparator = " \xC2\xB7 ";
constexpr uint32_t kInitialPanelWidth = 1280;
constexpr uint32_t kInitialPanelHeight = 720;

deskhub::MouseButton ButtonFromIndex(int button) {
    switch (button) {
        case 1: return deskhub::MouseButton::Right;
        case 2: return deskhub::MouseButton::Middle;
        case 3: return deskhub::MouseButton::X1;
        case 4: return deskhub::MouseButton::X2;
        default: return deskhub::MouseButton::Left;
    }
}

}

struct DhClientHandle {
    GpuChoice gpu;
    PanelRenderer renderer;
    std::atomic<uint32_t> negotiatedFps{0};

    DhClientStatsCallback statsCb = nullptr;
    DhClientSizeCallback sizeCb = nullptr;
    DhClientClosedCallback closedCb = nullptr;
    void* user = nullptr;

    std::atomic<bool> negotiated{false};
    std::atomic<bool> userStop{false};
    std::atomic<bool> closedNotified{false};

    deskhubp::ClientEngine<WinVideoDecoder, WinRenderTarget> engine{
        deskhub::diag::ClientDiagCaps{true, false}};

    void NotifyClosed(const char* reason) {
        if (closedNotified.exchange(true)) return;
        if (closedCb) closedCb(reason ? reason : "connection lost", user);
    }
};

namespace {

DhClientHandle* StartClient(const char* addrUtf8, uint8_t sourceId, uint64_t hwnd,
    DhClientStatsCallback statsCb, DhClientSizeCallback sizeCb,
    DhClientClosedCallback closedCb, void* user) {
    if (!addrUtf8) return nullptr;

    NetAddr server{};
    if (!ParseNetAddr(addrUtf8, server)) return nullptr;

    auto h = std::make_unique<DhClientHandle>();
    h->statsCb = statsCb;
    h->sizeCb = sizeCb;
    h->closedCb = closedCb;
    h->user = user;

    if (!CreateBestDevice({GpuVendor::Nvidia, GpuVendor::Intel, GpuVendor::Amd}, h->gpu))
        return nullptr;
    {
        Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(h->gpu.device.As(&mt))) mt->SetMultithreadProtected(TRUE);
    }
    if (!h->renderer.InitForHwnd(h->gpu.device.Get(), (void*)(uintptr_t)hwnd,
            kInitialPanelWidth, kInitialPanelHeight))
        return nullptr;

    DhClientHandle* raw = h.get();

    deskhubp::ClientEngineConfig cfg;
    cfg.server = server;
    cfg.sourceId = sourceId;
    cfg.screenW = uint32_t(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    cfg.screenH = uint32_t(GetSystemMetrics(SM_CYVIRTUALSCREEN));
    cfg.alwaysFocused = true;
    cfg.statusSeparator = kStatusSeparator;
    cfg.onParams = [raw](uint32_t width, uint32_t height, uint8_t fps) {
        raw->negotiatedFps.store(fps ? fps : 60, std::memory_order_relaxed);
        raw->negotiated.store(true, std::memory_order_release);
        if (raw->sizeCb) raw->sizeCb(width, height, raw->user);
    };
    cfg.onStatus = [raw](const char* compact) {
        if (raw->negotiated.load(std::memory_order_acquire) && raw->statsCb)
            raw->statsCb(compact, raw->user);
    };
    cfg.onEnded = [raw](const char* reason) { raw->NotifyClosed(reason); };
    cfg.onFinished = [raw](const char* reason) {
        if (!raw->userStop.load()) raw->NotifyClosed(reason && *reason ? reason : nullptr);
    };
    cfg.onDecodeThreadStart = [] { CoInitializeEx(nullptr, COINIT_MULTITHREADED); };
    cfg.onDecodeThreadExit = [] { CoUninitialize(); };

    raw->engine.SetSurface(
        WinRenderTarget{raw->gpu.device.Get(), &raw->renderer, &raw->negotiatedFps});

    if (!raw->engine.Start(cfg)) return nullptr;

    return h.release();
}

}

DH_API DhClientHandle* DH_CALL dh_client_start_hwnd(const char* addrUtf8, uint8_t sourceId,
    uint64_t hwnd, DhClientStatsCallback statsCb, DhClientSizeCallback sizeCb,
    DhClientClosedCallback closedCb, void* user) {
    if (!hwnd) return nullptr;
    return StartClient(addrUtf8, sourceId, hwnd, statsCb, sizeCb, closedCb, user);
}

DH_API void DH_CALL dh_client_mouse_move(DhClientHandle* h, uint16_t nx, uint16_t ny) {
    if (h) h->engine.QueueMouseMoveAbs(nx, ny);
}

DH_API void DH_CALL dh_client_mouse_move_rel(DhClientHandle* h, int dx, int dy) {
    if (h) h->engine.QueueMouseMoveRel(dx, dy);
}

DH_API void DH_CALL dh_client_mouse_button(DhClientHandle* h, int button, int down) {
    if (h) h->engine.QueueMouseButton(int32_t(ButtonFromIndex(button)), down != 0);
}

DH_API void DH_CALL dh_client_wheel(DhClientHandle* h, int delta) {
    if (h) h->engine.QueueMouseWheel(delta);
}

DH_API void DH_CALL dh_client_key(DhClientHandle* h, int vk, int scan, int down) {
    if (h) h->engine.QueueKey(vk, scan, down != 0);
}

DH_API void DH_CALL dh_client_stop(DhClientHandle* h) {
    if (!h) return;
    h->userStop.store(true);
    h->closedNotified.store(true);
    h->engine.Stop();
    delete h;
}
