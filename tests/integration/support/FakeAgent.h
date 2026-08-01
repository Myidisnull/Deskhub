#pragma once
#include "deskhubp/session/HostEngine.h"

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
        uint32_t fps = 30, uint32_t maxDim = 1920);

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

private:
    deskhubp::HostEngine engine_;
};

}
