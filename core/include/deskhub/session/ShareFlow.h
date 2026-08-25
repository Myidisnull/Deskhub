#pragma once
#include "deskhub/media/AgentTypes.h"
#include "deskhub/media/ShareSource.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/ui/UiSettings.h"

#include <utility>
#include <vector>

namespace deskhub {

struct ShareClampResult {
    std::vector<media::ShareSource> sources;
    bool clamped = false;
};

inline ShareClampResult ClampShareSources(std::vector<media::ShareSource> all) {
    ShareClampResult r;
    if (all.size() > kMaxSources) {
        all.resize(kMaxSources);
        r.clamped = true;
    }
    r.sources = std::move(all);
    return r;
}

inline media::AgentOptions ShareOptionsOf(const ui::UiSettings& settings) {
    media::AgentOptions options;
    options.fps = settings.fps;
    options.bitrateMbps = settings.bitrateMbps;
    options.maxDim = settings.maxDim;
    options.port = uint16_t(settings.port);
    options.allowInput = settings.allowInput;
    options.passcode = settings.passcode;
    options.bindIp = settings.bindIp;
    options.clipboardSync = settings.clipboardSync;
    options.encryptSession = settings.encryptSession;
    options.escrowSessionKey = settings.escrowSessionKey;
    options.audio = settings.shareAudio;
    options.acceptFiles = settings.acceptFiles;
    return options;
}

}
