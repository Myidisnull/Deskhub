#pragma once
#include "deskhub/media/AgentTypes.h"
#include "deskhub/protocol/Wire.h"

#include <cstdio>
#include <string>

namespace deskhub::media {

inline std::string ShareStatusTooltip(const AgentSourceStatus& s) {
    char tip[512];
    if (s.viewerConnected)
        std::snprintf(tip, sizeof(tip),
            "viewer %s\n%.0f fps sent \xC2\xB7 %.0f kbps \xC2\xB7 RTT %u ms\n"
            "capture %.0f fps \xC2\xB7 %s\nUDP port %u",
            s.viewerAddr.c_str(), s.sendFps, s.sendKbps, s.rttMs, s.captureFps,
            s.zeroCopy ? "zero-copy" : "CPU copy", unsigned(kDeskhubPort));
    else
        std::snprintf(tip, sizeof(tip), "waiting for a viewer\ncapture %.0f fps \xC2\xB7 %s\nUDP port %u",
            s.captureFps, s.zeroCopy ? "zero-copy" : "CPU copy", unsigned(kDeskhubPort));
    return tip;
}

}
