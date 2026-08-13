#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

#include <objbase.h>
#include <wrl/client.h>

#include "deskhubp/ffi/ClientSession.h"

#include <atomic>
#include <memory>
#include <string>

#include "gpu/GpuSelect.h"
#include "decode/PanelRenderer.h"
#include "decode/WinVideoDecoder.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/ffi/ClientSessionForward.h"
#include "deskhubp/ffi/ClientSessionShell.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/ClientEngine.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/UiSettingsStore.h"

namespace {

constexpr const char* kStatusSeparator = " \xC2\xB7 ";
constexpr uint32_t kInitialPanelWidth = 1280;
constexpr uint32_t kInitialPanelHeight = 720;

using WinClientEngine = deskhubp::ClientEngine<WinVideoDecoder, WinRenderTarget>;

}

struct DHSession : deskhubp::FfiClientSession<WinClientEngine> {
    DHSession() : FfiClientSession(deskhub::diag::ClientDiagCaps{true, false}) {}

    GpuChoice gpu;
    PanelRenderer renderer;
    std::atomic<uint32_t> negotiatedFps{0};
    std::atomic<bool> negotiated{false};
};

namespace {

WinClientEngine& EngineOf(DHSession* s) {
    return s->engine;
}

}

DHSession* dh_session_start(const char* address, uint8_t sourceId, void* surface,
    const DHSessionCallbacks* callbacks, const char* passcode) {
    if (!surface) return nullptr;

    NetAddr server{};
    if (!deskhubp::ParseSessionAddress(address, server)) return nullptr;

    auto session = std::make_unique<DHSession>();
    session->AdoptCallbacks(callbacks);

    if (!CreateBestDevice({GpuVendor::Nvidia, GpuVendor::Intel, GpuVendor::Amd}, session->gpu))
        return nullptr;
    {
        Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(session->gpu.device.As(&mt))) mt->SetMultithreadProtected(TRUE);
    }
    if (!session->renderer.InitForHwnd(session->gpu.device.Get(), surface, kInitialPanelWidth,
            kInitialPanelHeight))
        return nullptr;

    DHSession* raw = session.get();

    deskhubp::ClientEngineConfig cfg;
    cfg.server = server;
    cfg.sourceId = sourceId;
    cfg.screenW = uint32_t(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    cfg.screenH = uint32_t(GetSystemMetrics(SM_CYVIRTUALSCREEN));
    cfg.alwaysFocused = true;
    cfg.statusSeparator = kStatusSeparator;
    cfg.passcode = passcode ? passcode : "";
    cfg.displayName = deskhubp::SessionDeviceName();
    cfg.onParams = [raw](uint32_t width, uint32_t height, uint8_t fps) {
        raw->negotiatedFps.store(fps ? fps : 60, std::memory_order_relaxed);
        raw->negotiated.store(true, std::memory_order_release);
        if (raw->callbacks.onSize) raw->callbacks.onSize(width, height, raw->callbacks.user);
    };
    cfg.onStatus = [raw](const char* compact) {
        if (raw->negotiated.load(std::memory_order_acquire) && raw->callbacks.onStatus)
            raw->callbacks.onStatus(compact, raw->callbacks.user);
    };
    raw->WireLifecycle(cfg);
    cfg.onDecodeThreadStart = [] { CoInitializeEx(nullptr, COINIT_MULTITHREADED); };
    cfg.onDecodeThreadExit = [] { CoUninitialize(); };

    raw->engine.SetSurface(
        WinRenderTarget{raw->gpu.device.Get(), &raw->renderer, &raw->negotiatedFps});

    if (!raw->engine.Start(cfg)) return nullptr;

    return session.release();
}

void dh_session_stop(DHSession* s) {
    deskhubp::StopFfiClientSession(s);
}

void dh_session_set_layer(DHSession*, void*) {}

DESKHUB_DEFINE_CLIENT_SESSION_FORWARDERS(EngineOf)
