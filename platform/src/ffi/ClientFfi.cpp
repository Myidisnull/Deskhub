#include "deskhubp/ffi/ClientFfi.h"

#include "deskhub/input/Hotkeys.h"
#include "deskhub/input/PointerLockState.h"
#include "deskhub/input/Set1Scancodes.h"
#include "deskhub/input/VirtualKeys.h"
#include "deskhub/input/TrackpadCursor.h"
#include "deskhub/media/SourceLabel.h"
#include "deskhub/media/ViewerTitle.h"
#include "deskhub/media/ViewFit.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/OpenViewers.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/ffi/FfiText.h"
#include "deskhubp/net/SourceQuery.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

deskhub::ViewRect ToRect(DHViewRect r) {
    return {r.x, r.y, r.width, r.height};
}

deskhub::OpenViewerCount& OpenViewers() {
    static deskhub::OpenViewerCount count;
    return count;
}

template <class Fn>
DHPointerLockEffect ApplyPointerLock(DHPointerLock* state, Fn&& step) {
    if (!state) return DHPointerLockEffect{false, false, false};
    deskhub::PointerLockState s(state->locked, state->paused);
    const deskhub::PointerLockEffect e = step(s);
    state->locked = s.locked();
    state->paused = s.paused();
    return DHPointerLockEffect{e.lockChanged, e.pauseChanged, e.releaseHeldInput};
}

}

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
        case DHStrSessionEnded: return deskhub::ui::kSessionEnded;
        case DHStrShareStartFailed: return deskhub::ui::kShareStartFailed;
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

int dh_connecting_to(const char* address, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity), deskhub::ui::ConnectingTo(address ? address : ""));
    return int(std::strlen(out));
}

int dh_host_title(const char* address, uint32_t width, uint32_t height, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity),
        deskhub::ui::HostTitle(address ? address : "", width, height));
    return int(std::strlen(out));
}

int dh_zoom_label(double zoom, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    std::snprintf(out, size_t(capacity), "%.1f\xC3\x97", zoom);
    return int(std::strlen(out));
}

bool dh_is_zoomed(double zoom) {
    return deskhub::IsZoomed(zoom);
}

int dh_viewer_base_title(const char* sourceName, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity),
        deskhub::ViewerBaseTitle(sourceName ? sourceName : ""));
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
        const deskhub::SourceInfo& src = sources[size_t(i)];
        out[i].sourceId = src.sourceId;
        out[i].width = src.width;
        out[i].height = src.height;
        deskhubp::CopyToBuf(out[i].name, sizeof(out[i].name), src.name);
        deskhubp::CopyToBuf(out[i].displayName, sizeof(out[i].displayName),
            deskhub::media::SourceName(src.name, src.sourceId));
        deskhubp::CopyToBuf(out[i].sizeLabel, sizeof(out[i].sizeLabel),
            deskhub::media::SourceSizeLabel(src.width, src.height));
        deskhubp::CopyToBuf(out[i].pickerLabel, sizeof(out[i].pickerLabel),
            deskhub::media::SourcePickerLabel(src.name, src.sourceId, src.width, src.height));
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
    return deskhub::NormalizePointer(px, py, ToRect(rect), *nx, *ny);
}

int32_t dh_take_scroll_notches(double dragPoints, double* carry) {
    if (!carry) return 0;
    return deskhub::TakeScrollNotches(dragPoints, *carry);
}

DHCursor dh_cursor_clamp(DHCursor cur, DHViewRect video, double viewportW, double viewportH) {
    const deskhub::TrackpadCursor out = deskhub::ClampToVisible({cur.x, cur.y}, ToRect(video),
        viewportW, viewportH);
    return DHCursor{out.x, out.y};
}

DHCursor dh_cursor_move(DHCursor cur, double dx, double dy, DHViewRect video, double viewportW,
    double viewportH) {
    const deskhub::TrackpadCursor out = deskhub::MoveCursorBy({cur.x, cur.y}, dx, dy,
        ToRect(video), viewportW, viewportH);
    return DHCursor{out.x, out.y};
}

bool dh_cursor_point(DHCursor cur, DHViewRect video, double* px, double* py) {
    if (!px || !py) return false;
    return deskhub::CursorScreenPoint({cur.x, cur.y}, ToRect(video), *px, *py);
}

bool dh_cursor_normalize(DHCursor cur, DHViewRect video, int32_t* nx, int32_t* ny) {
    if (!nx || !ny) return false;
    return deskhub::NormalizeCursor({cur.x, cur.y}, ToRect(video), *nx, *ny);
}

DHModifier dh_modifier_class(int32_t vk) {
    switch (deskhub::ModifierKeyOf(vk)) {
        case deskhub::ModifierKey::Shift: return DHModifierShift;
        case deskhub::ModifierKey::Control: return DHModifierControl;
        case deskhub::ModifierKey::Menu: return DHModifierOption;
        case deskhub::ModifierKey::Win: return DHModifierCommand;
        case deskhub::ModifierKey::CapsLock: return DHModifierCapsLock;
        case deskhub::ModifierKey::None: return DHModifierNone;
    }
    return DHModifierNone;
}

int32_t dh_vk_scancode(int32_t vk) {
    return deskhub::VkToSet1Scancode(vk);
}

DHPointerLockEffect dh_pointer_toggle_lock(DHPointerLock* state) {
    return ApplyPointerLock(state, [](deskhub::PointerLockState& s) { return s.OnToggleLockKey(); });
}

DHPointerLockEffect dh_pointer_toggle_pause(DHPointerLock* state) {
    return ApplyPointerLock(state,
        [](deskhub::PointerLockState& s) { return s.OnTogglePauseKey(); });
}

DHPointerLockEffect dh_pointer_escape(DHPointerLock* state) {
    return ApplyPointerLock(state, [](deskhub::PointerLockState& s) { return s.OnEscape(); });
}

DHPointerLockEffect dh_pointer_focus_lost(DHPointerLock* state) {
    return ApplyPointerLock(state, [](deskhub::PointerLockState& s) { return s.OnFocusLost(); });
}

int dh_pointer_subtitle(DHPointerLock state, const char* statusLine, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const deskhub::PointerLockState s(state.locked, state.paused);
    deskhubp::CopyToBuf(out, size_t(capacity), s.SubtitleFor(statusLine ? statusLine : ""));
    return int(std::strlen(out));
}

void dh_viewer_opened() {
    OpenViewers().Opened();
}

bool dh_viewer_closed() {
    return OpenViewers().Closed();
}

int dh_viewer_count() {
    return OpenViewers().count();
}
}
