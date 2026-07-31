#include <TargetConditionals.h>

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <CoreGraphics/CoreGraphics.h>
#endif

#include "deskhubp/ffi/ClientSession.h"

#include <cstring>
#include <memory>
#include <string>

#include "deskhubp/diag/Log.h"
#include "deskhubp/media/VtDecoder.h"
#include "deskhubp/session/ClientEngine.h"

namespace {

using AppleClientEngine = deskhubp::ClientEngine<VtDecoder, void*>;

constexpr uint32_t kMaxDisplays = 16;

void LocalScreenPixels(uint32_t& outW, uint32_t& outH) {
    outW = outH = 0;
#if TARGET_OS_IPHONE
    UIScreen* screen = UIScreen.mainScreen;
    if (!screen) return;
    const CGRect bounds = screen.nativeBounds;
    if (bounds.size.width <= 0 || bounds.size.height <= 0) return;
    outW = uint32_t(bounds.size.width);
    outH = uint32_t(bounds.size.height);
#else
    CGDirectDisplayID ids[kMaxDisplays];
    uint32_t count = 0;
    if (CGGetActiveDisplayList(kMaxDisplays, ids, &count) != kCGErrorSuccess || !count) return;
    uint64_t bestArea = 0;
    for (uint32_t i = 0; i < count; ++i) {
        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(ids[i]);
        if (!mode) continue;
        const uint64_t w = CGDisplayModeGetPixelWidth(mode);
        const uint64_t h = CGDisplayModeGetPixelHeight(mode);
        CGDisplayModeRelease(mode);
        if (!w || !h || w * h <= bestArea) continue;
        bestArea = w * h;
        outW = uint32_t(w);
        outH = uint32_t(h);
    }
#endif
}

void CopyToBuf(char* dst, size_t cap, const std::string& s) {
    const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

}

struct DHSession {
    DHSession() : engine(deskhub::diag::ClientDiagCaps{false, true}) {}

    AppleClientEngine engine;
    char statusBuf[256] = {};
    char reasonBuf[256] = {};
};

DHSession* dh_session_start(const char* address, uint8_t sourceId) {
    if (!address) return nullptr;
    NetAddr server;
    if (!ParseNetAddr(address, server)) {
        LOGE("[Bridge] Invalid address: %s", address);
        return nullptr;
    }

    deskhubp::ClientEngineConfig cfg;
    cfg.server = server;
    cfg.sourceId = sourceId;
    LocalScreenPixels(cfg.screenW, cfg.screenH);

    auto session = std::make_unique<DHSession>();
    if (!session->engine.Start(cfg)) return nullptr;
    return session.release();
}

void dh_session_stop(DHSession* s) {
    if (!s) return;
    s->engine.Stop();
    delete s;
}

void dh_session_set_layer(DHSession* s, void* layer) {
    if (s) s->engine.SetSurface(layer);
}

void dh_session_key(DHSession* s, int32_t vk, int32_t scan, bool down) {
    if (s) s->engine.QueueKey(vk, scan, down);
}

void dh_session_key_tap(DHSession* s, int32_t vk, int32_t scan) {
    if (s) s->engine.QueueKeyTap(vk, scan);
}

void dh_session_key_chord(DHSession* s, int32_t modVk, int32_t modScan, int32_t vk, int32_t scan) {
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
