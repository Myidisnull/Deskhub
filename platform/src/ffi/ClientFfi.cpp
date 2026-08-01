#include "deskhubp/ffi/ClientFfi.h"

#include "deskhub/input/Hotkeys.h"
#include "deskhub/media/SourceLabel.h"
#include "deskhub/media/ViewerTitle.h"
#include "deskhub/media/ViewFit.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/ffi/FfiText.h"
#include "deskhubp/net/SourceQuery.h"

#include <cstring>
#include <string>
#include <vector>

extern "C" {

const char* dh_string(DHStringId id) {
    switch (id) {
        case DHStrAppTitle: return deskhub::ui::kAppTitle;
        case DHStrHostIpIntro: return deskhub::ui::kHostIpIntro;
        case DHStrNoNetworkAddress: return deskhub::ui::kNoNetworkAddress;
        case DHStrClientIpPrompt: return deskhub::ui::kClientIpPrompt;
        case DHStrPickerTitle: return deskhub::ui::kPickerTitle;
        case DHStrPickerEachWindow: return deskhub::ui::kPickerEachWindow;
        case DHStrShareButton: return deskhub::ui::kShareButton;
        case DHStrSharingTitle: return deskhub::ui::kSharingTitle;
        case DHStrSharingSourcesIntro: return deskhub::ui::kSharingSourcesIntro;
        case DHStrSharingConnectHint: return deskhub::ui::kSharingConnectHint;
        case DHStrNothingShared: return deskhub::ui::kNothingShared;
        case DHStrStopSharing: return deskhub::ui::kStopSharing;
        case DHStrQueryingSources: return deskhub::ui::kQueryingSources;
        case DHStrViewerOpenFailed: return deskhub::ui::kViewerOpenFailed;
        case DHStrConnectionEndedTitle: return deskhub::ui::kConnectionEndedTitle;
        case DHStrDisconnected: return deskhub::ui::kDisconnected;
        case DHStrUdpPortLine: {
            static const std::string line = deskhub::ui::UdpPortLine();
            return line.c_str();
        }
        case DHStrInvalidAddressHint: {
            static const std::string hint = deskhub::ui::InvalidAddressHint();
            return hint.c_str();
        }
    }
    return "";
}

bool dh_parse_address(const char* address) {
    if (!address) return false;
    NetAddr server;
    return ParseNetAddr(address, server);
}

int dh_source_picker_label(const char* name, uint8_t sourceId, uint16_t width, uint16_t height,
    char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const std::string label = deskhub::media::SourcePickerLabel(name ? name : "", sourceId,
        width, height);
    deskhubp::CopyToBuf(out, size_t(capacity), label);
    return int(std::strlen(out));
}

int dh_shared_source_label(const char* name, uint16_t width, uint16_t height,
    bool viewerConnected, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const std::string label = deskhub::media::SharedSourceLabel(name ? name : "", width, height,
        viewerConnected);
    deskhubp::CopyToBuf(out, size_t(capacity), label);
    return int(std::strlen(out));
}

int dh_viewer_base_title(const char* sourceName, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity),
        deskhub::ViewerBaseTitle(sourceName ? sourceName : ""));
    return int(std::strlen(out));
}

int dh_viewer_subtitle(const char* statusLine, bool mouseLocked, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity),
        deskhub::ViewerStatusWithHint(statusLine ? statusLine : "", mouseLocked));
    return int(std::strlen(out));
}

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

int dh_hotkeys(DHHotkey* out, int capacity) {
    if (!out || capacity <= 0) return 0;

    const std::span<const deskhub::Hotkey> table = deskhub::TouchHotkeys();
    const int count = int(table.size()) < capacity ? int(table.size()) : capacity;
    for (int i = 0; i < count; ++i) {
        const deskhub::Hotkey& h = table[size_t(i)];
        const size_t room = sizeof(out[i].label) - 1;
        const size_t n = std::strlen(h.label) < room ? std::strlen(h.label) : room;
        std::memcpy(out[i].label, h.label, n);
        out[i].label[n] = '\0';
        out[i].vk = h.vk;
        out[i].scan = h.scan;
        out[i].modVk = h.modVk;
        out[i].modScan = h.modScan;
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
