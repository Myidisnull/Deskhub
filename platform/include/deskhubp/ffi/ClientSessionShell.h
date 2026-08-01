#pragma once
#include "deskhub/diag/ClientDiag.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/ffi/ClientSession.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/ClientEngine.h"

#include <atomic>

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

    explicit FfiClientSession(deskhub::diag::ClientDiagCaps caps = {}) : engine(caps) {}

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

}
