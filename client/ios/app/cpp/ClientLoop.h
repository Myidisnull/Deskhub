#pragma once
#include "deskhubp/media/VtDecoder.h"
#include "deskhubp/session/ClientEngine.h"

class ClientLoop : public deskhubp::ClientEngine<VtDecoder, void*> {
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

    void SetLayer(void* layer) {
        SetSurface(layer);
    }

    bool Finished() const {
        return finished();
    }
};
