#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

#include <objbase.h>
#include <wrl/client.h>

#include "deskhubp/ffi/ClientSession.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>

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

void CopyToBuf(char* dst, size_t cap, const std::string& s) {
    const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

}

struct DHSession {
    DHSession() : engine(deskhub::diag::ClientDiagCaps{true, false}) {}

    deskhubp::ClientEngine<WinVideoDecoder, WinRenderTarget> engine;

    GpuChoice gpu;
    PanelRenderer renderer;
    std::atomic<uint32_t> negotiatedFps{0};

    DHSessionCallbacks callbacks{};

    std::atomic<bool> negotiated{false};
    std::atomic<bool> userStop{false};
    std::atomic<bool> closedNotified{false};

    char statusBuf[256] = {};
    char reasonBuf[256] = {};

    void NotifyClosed(const char* reason) {
        if (closedNotified.exchange(true)) return;
        if (callbacks.onClosed)
            callbacks.onClosed(reason ? reason : "connection lost", callbacks.user);
    }
};

DHSession* dh_session_start(const char* address, uint8_t sourceId, void* surface,
    const DHSessionCallbacks* callbacks) {
    if (!address || !surface) return nullptr;

    NetAddr server{};
    if (!ParseNetAddr(address, server)) {
        LOGE("[Bridge] Invalid address: %s", address);
        return nullptr;
    }

    auto session = std::make_unique<DHSession>();
    if (callbacks) session->callbacks = *callbacks;

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
    cfg.onParams = [raw](uint32_t width, uint32_t height, uint8_t fps) {
        raw->negotiatedFps.store(fps ? fps : 60, std::memory_order_relaxed);
        raw->negotiated.store(true, std::memory_order_release);
        if (raw->callbacks.onSize) raw->callbacks.onSize(width, height, raw->callbacks.user);
    };
    cfg.onStatus = [raw](const char* compact) {
        if (raw->negotiated.load(std::memory_order_acquire) && raw->callbacks.onStatus)
            raw->callbacks.onStatus(compact, raw->callbacks.user);
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

    return session.release();
}

void dh_session_stop(DHSession* s) {
    if (!s) return;
    s->userStop.store(true);
    s->closedNotified.store(true);
    s->engine.Stop();
    delete s;
}

void dh_session_set_layer(DHSession*, void*) {}

void dh_session_key(DHSession* s, int32_t vk, int32_t scan, bool down) {
    if (s) s->engine.QueueKey(vk, scan, down);
}

void dh_session_key_tap(DHSession* s, int32_t vk, int32_t scan) {
    if (s) s->engine.QueueKeyTap(vk, scan);
}

void dh_session_key_chord(DHSession* s, int32_t modVk, int32_t modScan, int32_t vk,
    int32_t scan) {
    if (s) s->engine.QueueKeyChord(modVk, modScan, vk, scan);
}

void dh_session_char_tap(DHSession* s, uint32_t codepoint) {
    if (s) s->engine.QueueCharTap(codepoint);
}

void dh_session_release_all_input(DHSession* s) {
    if (s) s->engine.ReleaseAllInput();
}

void dh_session_mouse_move(DHSession* s, int32_t nx, int32_t ny) {
    if (s) s->engine.QueueMouseMoveAbs(nx, ny);
}

void dh_session_mouse_move_rel(DHSession* s, int32_t dx, int32_t dy) {
    if (s) s->engine.QueueMouseMoveRel(dx, dy);
}

void dh_session_mouse_button(DHSession* s, int32_t button, bool down) {
    if (s) s->engine.QueueMouseButton(button, down);
}

void dh_session_mouse_wheel(DHSession* s, int32_t delta) {
    if (s) s->engine.QueueMouseWheel(delta);
}

DHPhase dh_session_phase(DHSession* s) {
    if (!s) return DHPhaseIdle;
    return DHPhase(int(s->engine.phase()));
}

const char* dh_session_status_line(DHSession* s) {
    if (!s) return "";
    CopyToBuf(s->statusBuf, sizeof(s->statusBuf), s->engine.StatusLine());
    return s->statusBuf;
}

const char* dh_session_end_reason(DHSession* s) {
    if (!s) return "";
    CopyToBuf(s->reasonBuf, sizeof(s->reasonBuf), s->engine.EndReason());
    return s->reasonBuf;
}

uint32_t dh_session_video_width(DHSession* s) {
    return s ? s->engine.videoWidth() : 0;
}

uint32_t dh_session_video_height(DHSession* s) {
    return s ? s->engine.videoHeight() : 0;
}
