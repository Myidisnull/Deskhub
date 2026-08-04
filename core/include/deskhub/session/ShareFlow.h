#pragma once
#include "deskhub/media/ShareSource.h"
#include "deskhub/protocol/Wire.h"

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

}
