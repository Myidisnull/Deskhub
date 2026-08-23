#include "deskhubp/host/SharingHost.h"

bool SharingHost::Start(const std::vector<ShareSource>& sources, const ShareOptions& opt) {
    deskhubp::HostEnginePolicy policy;
    policy.noSourceError = "Nothing to share.";
    policy.preflight = [] {
        return std::string(
            "The Deskhub app hosts file transfer only \xE2\x80\x94 screen sharing runs in the "
            "broadcast extension.");
    };
    return StartEngine(sources, opt, std::move(policy));
}
