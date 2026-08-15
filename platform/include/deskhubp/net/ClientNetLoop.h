#pragma once
#include "deskhub/session/ClientPump.h"
#include "deskhubp/net/SessionTransport.h"

#include <cstdint>
#include <functional>

namespace deskhubp {

uint32_t MakeClientId(uint8_t sourceId);

struct ClientNetLoopHooks {
    std::function<bool()> stopped;
    std::function<void(deskhub::ClientPump&, uint64_t nowUs)> afterFrames;
    std::function<void(deskhub::ClientPump&, uint64_t nowUs)> beforeTick;
    std::function<void(bool streaming)> onPhase;
    std::function<void()> onSocketError;
};

void RunClientNetLoop(SessionTransport& sock, deskhub::ClientPump& pump,
    const ClientNetLoopHooks& hooks);

}
