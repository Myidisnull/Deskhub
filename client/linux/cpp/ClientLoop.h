#pragma once
#include "decode/AvDecoder.h"
#include "deskhubp/session/ClientEngine.h"

class VideoSink;

class ClientLoop : public deskhubp::ClientEngine<AvDecoder, VideoSink*> {
public:
    using Phase = deskhubp::ClientPhase;

    bool Start(const NetAddr& server, uint8_t sourceId, VideoSink* sink, uint32_t screenW,
        uint32_t screenH) {
        SetSurface(sink);

        deskhubp::ClientEngineConfig cfg;
        cfg.server = server;
        cfg.sourceId = sourceId;
        cfg.screenW = screenW;
        cfg.screenH = screenH;
        return ClientEngine::Start(cfg);
    }
};
