#pragma once
#include <android/native_window.h>

#include "decode/MediaCodecDecoder.h"
#include "deskhubp/session/ClientEngine.h"

class ClientLoop : public deskhubp::ClientEngine<MediaCodecDecoder, ANativeWindow*> {
public:
    using Phase = deskhubp::ClientPhase;

    ClientLoop() : ClientEngine(deskhub::diag::ClientDiagCaps{false, true}) {}

    bool Start(const NetAddr& server, uint8_t sourceId, uint32_t screenW, uint32_t screenH) {
        deskhubp::ClientEngineConfig cfg;
        cfg.server = server;
        cfg.sourceId = sourceId;
        cfg.screenW = screenW;
        cfg.screenH = screenH;
        return ClientEngine::Start(cfg);
    }

    void SetWindow(ANativeWindow* window) {
        SetSurface(window);
    }

    bool Finished() const {
        return finished();
    }
};
