#include "deskhubp/session/AgentLoop.h"

bool AgentLoop::Start(const std::vector<AgentSource>& sources, const AgentOptions& opt) {
    deskhubp::HostEnginePolicy policy;
    policy.noSourceError = "Nothing to share.";
    policy.preflight = [] {
        return std::string(
            "The Deskhub app hosts file transfer only \xE2\x80\x94 screen sharing runs in the "
            "broadcast extension.");
    };
    return StartEngine(sources, opt, std::move(policy));
}
