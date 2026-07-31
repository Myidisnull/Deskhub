#pragma once
#include "deskhubp/session/HostEngine.h"

#include <string>
#include <vector>

using AgentSource = deskhub::media::ShareSource;
using AgentOptions = deskhub::media::AgentOptions;
using AgentSourceStatus = deskhub::media::AgentSourceStatus;

class AgentLoop {
public:
    AgentLoop() = default;
    AgentLoop(const AgentLoop&) = delete;
    AgentLoop& operator=(const AgentLoop&) = delete;

    bool Start(const std::vector<AgentSource>& sources, const AgentOptions& opt);

    void Stop() {
        engine_.Stop();
    }

    bool running() const {
        return engine_.running();
    }

    std::vector<AgentSourceStatus> Status() {
        return engine_.Status();
    }

    std::string LastError() {
        return engine_.LastError();
    }

private:
    deskhubp::HostEngine engine_;
};
