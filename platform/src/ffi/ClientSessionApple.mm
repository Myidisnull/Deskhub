#include <TargetConditionals.h>

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <CoreGraphics/CoreGraphics.h>
#endif

#include "deskhubp/ffi/ClientSession.h"

#include <memory>
#include <string>

#include "deskhubp/diag/Log.h"
#include "deskhubp/ffi/ClientSessionForward.h"
#include "deskhubp/ffi/ClientSessionShell.h"
#include "deskhubp/media/VtDecoder.h"
#include "deskhubp/session/ClientEngine.h"

namespace {

using AppleClientEngine = deskhubp::ClientEngine<VtDecoder, void*>;

constexpr uint32_t kMaxDisplays = 16;

#if TARGET_OS_IPHONE
UIScreen* ActiveScreen() {
    UIScreen* fallback = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class]) continue;
        UIWindowScene* windowScene = (UIWindowScene*)scene;
        if (scene.activationState == UISceneActivationStateForegroundActive)
            return windowScene.screen;
        if (!fallback) fallback = windowScene.screen;
    }
    return fallback;
}
#endif

void LocalScreenPixels(uint32_t& outW, uint32_t& outH) {
    outW = outH = 0;
#if TARGET_OS_IPHONE
    UIScreen* screen = ActiveScreen();
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

}

struct DHSession : deskhubp::FfiClientSession<AppleClientEngine> {
    DHSession() : FfiClientSession(deskhub::diag::ClientDiagCaps{false, true}) {}
};

namespace {

AppleClientEngine& EngineOf(DHSession* s) {
    return s->engine;
}

}

DHSession* dh_session_start(const char* address, uint8_t sourceId, void* surface,
    const DHSessionCallbacks* callbacks) {
    NetAddr server;
    if (!deskhubp::ParseSessionAddress(address, server)) return nullptr;

    auto session = std::make_unique<DHSession>();
    session->AdoptCallbacks(callbacks);
    DHSession* raw = session.get();

    deskhubp::ClientEngineConfig cfg;
    cfg.server = server;
    cfg.sourceId = sourceId;
    LocalScreenPixels(cfg.screenW, cfg.screenH);
    cfg.onParams = [raw](uint32_t width, uint32_t height, uint8_t) {
        if (raw->callbacks.onSize) raw->callbacks.onSize(width, height, raw->callbacks.user);
    };
    cfg.onStatus = [raw](const char* compact) {
        if (raw->callbacks.onStatus) raw->callbacks.onStatus(compact, raw->callbacks.user);
    };
    raw->WireLifecycle(cfg);

    if (surface) session->engine.SetSurface(surface);
    if (!session->engine.Start(cfg)) return nullptr;
    return session.release();
}

void dh_session_stop(DHSession* s) {
    if (!s) return;
    s->StopQuietly();
    delete s;
}

void dh_session_set_layer(DHSession* s, void* layer) {
    if (s) s->engine.SetSurface(layer);
}

DESKHUB_DEFINE_CLIENT_SESSION_FORWARDERS(EngineOf)
