#pragma once
#include <android/native_window.h>

#include "decode/MediaCodecDecoder.h"
#include "deskhubp/session/ClientEngine.h"

class ClientLoop : public deskhubp::ClientEngine<MediaCodecDecoder, ANativeWindow*> {
public:
    using Phase = deskhubp::ClientPhase;

    ClientLoop() : ClientEngine(deskhub::diag::ClientDiagCaps{false, true}) {}

    void SetWindow(ANativeWindow* window) {
        SetSurface(window);
    }

    bool Finished() const {
        return finished();
    }
};
