#pragma once
#include "support/TestSupport.h"

#include "deskhubp/session/HostEngine.h"

#include <mutex>
#include <string>
#include <vector>

namespace fake {

deskhub::media::ShareSource Source(const char* name, uint32_t width, uint32_t height,
    uint64_t targetId);

class Agent {
public:
    ~Agent() {
        Stop();
    }

    bool Start(const std::vector<deskhub::media::ShareSource>& sources, uint16_t port,
        uint32_t fps = 30, uint32_t maxDim = 1920, const std::string& passcode = kTestPasscode,
        bool allowInput = true);

    void Stop() {
        engine_.Stop();
    }

    bool running() {
        return engine_.running();
    }

    std::vector<deskhub::media::AgentSourceStatus> Status() {
        return engine_.Status();
    }

    std::string LastError() {
        return engine_.LastError();
    }

    std::vector<uint64_t> TakePairingRequests() {
        const std::lock_guard<std::mutex> lock(pairMutex_);
        std::vector<uint64_t> out;
        out.swap(pairingAsks_);
        return out;
    }

    void AnswerPairing(uint64_t addrPacked, bool allowed) {
        engine_.AnswerPairingRequest(addrPacked, allowed);
    }

    void PushPairingRequest(uint64_t addrPacked) {
        const std::lock_guard<std::mutex> lock(pairMutex_);
        pairingAsks_.push_back(addrPacked);
    }

private:
    deskhubp::HostEngine engine_;
    std::mutex pairMutex_;
    std::vector<uint64_t> pairingAsks_;
};

}
