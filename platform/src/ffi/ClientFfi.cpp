#include "deskhubp/ffi/ClientFfi.h"

#include "deskhub/media/ViewFit.h"
#include "deskhub/protocol/Wire.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/net/SourceQuery.h"

#include <cstring>
#include <string>
#include <vector>

extern "C" {

int dh_list_sources(const char* address, DHSourceInfo* out, int capacity) {
    if (!address || !out || capacity <= 0) return 0;

    NetAddr server;
    if (!ParseNetAddr(address, server)) {
        LOGE("[Bridge] Invalid address: %s", address);
        return 0;
    }

    std::vector<deskhub::SourceInfo> sources;
    if (!QuerySources(server, sources)) return 0;

    const int count = int(sources.size()) < capacity ? int(sources.size()) : capacity;
    for (int i = 0; i < count; ++i) {
        out[i].sourceId = sources[size_t(i)].sourceId;
        out[i].width = sources[size_t(i)].width;
        out[i].height = sources[size_t(i)].height;
        const std::string& name = sources[size_t(i)].name;
        const size_t room = sizeof(out[i].name) - 1;
        const size_t n = name.size() < room ? name.size() : room;
        std::memcpy(out[i].name, name.data(), n);
        out[i].name[n] = '\0';
    }
    return count;
}

DHViewRect dh_video_rect(double viewportW, double viewportH, double aspect, DHViewTransform t) {
    const deskhub::ViewRect r = deskhub::FitVideoRect(viewportW, viewportH, aspect,
        deskhub::ViewTransform{t.zoom, t.panX, t.panY});
    return DHViewRect{r.x, r.y, r.width, r.height};
}

DHViewTransform dh_apply_gesture(DHViewTransform cur, double factor, double centroidX,
    double centroidY, double panDeltaX, double panDeltaY, double viewportW, double viewportH,
    double aspect) {
    const deskhub::ViewTransform t = deskhub::ApplyGesture(
        deskhub::ViewTransform{cur.zoom, cur.panX, cur.panY}, factor, centroidX, centroidY,
        panDeltaX, panDeltaY, viewportW, viewportH, aspect);
    return DHViewTransform{t.zoom, t.panX, t.panY};
}

bool dh_normalize_pointer(double px, double py, DHViewRect rect, int32_t* nx, int32_t* ny) {
    if (!nx || !ny) return false;
    return deskhub::NormalizePointer(px, py,
        deskhub::ViewRect{rect.x, rect.y, rect.width, rect.height}, *nx, *ny);
}
}
