#include "ClientSessionAndroid.h"

#include <android/native_window.h>

#include <atomic>
#include <memory>

#include "decode/MediaCodecDecoder.h"
#include "deskhubp/ffi/ClientSessionForward.h"
#include "deskhubp/ffi/ClientSessionShell.h"
#include "deskhubp/session/ClientEngine.h"

namespace {

using AndroidClientEngine = deskhubp::ClientEngine<MediaCodecDecoder, ANativeWindow*>;

std::atomic<uint32_t> g_screenW{0};
std::atomic<uint32_t> g_screenH{0};

}

void dh_session_set_screen_hint(uint32_t width, uint32_t height) {
    g_screenW.store(width, std::memory_order_relaxed);
    g_screenH.store(height, std::memory_order_relaxed);
}

struct DHSession : deskhubp::FfiClientSession<AndroidClientEngine> {
    DHSession()
        : FfiClientSession(deskhub::diag::ClientDiagCaps{false, true}) {}
};

namespace {

AndroidClientEngine& EngineOf(DHSession* s) {
    return s->engine;
}

}

DHSession* dh_session_start(const char* address, uint8_t sourceId, void* surface,
    const DHSessionCallbacks* callbacks, const char* passcode, const char* session_key) {
    return deskhubp::StartFfiClientSession<DHSession, ANativeWindow*>(address, sourceId, surface,
        callbacks, g_screenW.load(std::memory_order_relaxed),
        g_screenH.load(std::memory_order_relaxed), passcode, session_key);
}

void dh_session_stop(DHSession* s) {
    deskhubp::StopFfiClientSession(s);
}

void dh_session_set_layer(DHSession* s, void* layer) {
    if (s) s->engine.SetSurface(static_cast<ANativeWindow*>(layer));
}

DESKHUB_DEFINE_CLIENT_SESSION_FORWARDERS(EngineOf)
