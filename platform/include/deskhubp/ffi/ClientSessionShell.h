#pragma once
#include "deskhub/diag/ClientDiag.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/ffi/ClientSession.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/ClientEngine.h"
#include "deskhubp/system/UiSettingsStore.h"

#include <atomic>
#include <memory>

namespace deskhubp {

inline bool ParseSessionAddress(const char* address, NetAddr& server) {
    if (!address) return false;
    if (ParseNetAddr(address, server)) return true;
    LOGE("[Bridge] Invalid address: %s", address);
    return false;
}

template <class Engine>
struct FfiClientSession {
    Engine engine;
    DHSessionCallbacks callbacks{};
    std::atomic<bool> userStop{false};
    std::atomic<bool> closedNotified{false};
    char statusBuf[256] = {};
    char reasonBuf[256] = {};

    explicit FfiClientSession(deskhub::diag::ClientDiagCaps caps = {})
        : engine(caps) {}

    void AdoptCallbacks(const DHSessionCallbacks* cb) {
        if (cb) callbacks = *cb;
    }

    void NotifyClosed(const char* reason) {
        if (closedNotified.exchange(true)) return;
        if (callbacks.onClosed)
            callbacks.onClosed(reason && *reason ? reason : "connection lost", callbacks.user);
    }

    void WireLifecycle(ClientEngineConfig& cfg) {
        cfg.onEnded = [this](const char* reason) { NotifyClosed(reason); };
        cfg.onFinished = [this](const char* reason) {
            if (!userStop.load()) NotifyClosed(reason);
        };
    }

    void StopQuietly() {
        userStop.store(true);
        closedNotified.store(true);
        engine.Stop();
    }
};

template <class Session, class Surface>
Session* StartFfiClientSession(const char* address, uint8_t sourceId, void* surface,
    const DHSessionCallbacks* callbacks, uint32_t screenW, uint32_t screenH,
    const char* passcode = nullptr, const char* sessionKey = nullptr) {
    NetAddr server;
    if (!ParseSessionAddress(address, server)) return nullptr;

    auto session = std::make_unique<Session>();
    session->AdoptCallbacks(callbacks);
    Session* raw = session.get();

    ClientEngineConfig cfg;
    cfg.server = server;
    cfg.sourceId = sourceId;
    cfg.screenW = screenW;
    cfg.screenH = screenH;
    cfg.passcode = passcode ? passcode : "";
    cfg.sessionKeyHex = sessionKey ? sessionKey : "";
    cfg.displayName = SessionDeviceName();
    cfg.wantsAudio = LoadUiSettings().playAudio;
    cfg.onParams = [raw](uint32_t width, uint32_t height, uint8_t) {
        if (raw->callbacks.onSize) raw->callbacks.onSize(width, height, raw->callbacks.user);
    };
    cfg.onStatus = [raw](const char* compact) {
        if (raw->callbacks.onStatus) raw->callbacks.onStatus(compact, raw->callbacks.user);
    };
    raw->WireLifecycle(cfg);

    if (surface) raw->engine.SetSurface(static_cast<Surface>(surface));
    if (!raw->engine.Start(cfg)) return nullptr;
    return session.release();
}

template <class Session>
void StopFfiClientSession(Session* s) {
    if (!s) return;
    s->StopQuietly();
    delete s;
}

}
