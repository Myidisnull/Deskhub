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
#include "deskhub/session/ConnectFlow.h"
#include "deskhub/session/OpenViewers.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/ffi/FfiText.h"
#include "deskhubp/input/NativeKeyMap.h"
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
    if (!state) return DHPointerLockEffect{false, false};
    deskhub::PointerLockState s(state->locked);
    const deskhub::PointerLockEffect e = step(s);
    state->locked = s.locked();
    return DHPointerLockEffect{e.lockChanged, e.releaseHeldInput};
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
        case DHStrClientPasscodePrompt: return deskhub::ui::kClientPasscodePrompt;
        case DHStrClientPasscodeHint: return deskhub::ui::kClientPasscodeHint;
        case DHStrPasscodeInvalid: return deskhub::ui::kPasscodeInvalid;
        case DHStrPasscodeLabel: return deskhub::ui::kPasscodeLabel;
        case DHStrLanDevicesHeading: return deskhub::ui::kLanDevicesHeading;
        case DHStrRecentDevicesHeading: return deskhub::ui::kRecentDevicesHeading;
        case DHStrRecentDevicesHint: return deskhub::ui::kRecentDevicesHint;
        case DHStrRecentDevicesEmpty: return deskhub::ui::kRecentDevicesEmpty;
        case DHStrSidebarHost: return deskhub::ui::kSidebarHost;
        case DHStrSidebarClient: return deskhub::ui::kSidebarClient;
        case DHStrSidebarSettings: return deskhub::ui::kSidebarSettings;
        case DHStrHostHeading: return deskhub::ui::kHostHeading;
        case DHStrClientHeading: return deskhub::ui::kClientHeading;
        case DHStrSettingsHeading: return deskhub::ui::kSettingsHeading;
        case DHStrSettingsHint: return deskhub::ui::kSettingsHint;
        case DHStrClientSettingsHeading: return deskhub::ui::kClientSettingsHeading;
        case DHStrClientSettingsHint: return deskhub::ui::kClientSettingsHint;
        case DHStrUdpPortLabel: return deskhub::ui::kUdpPortLabel;
        case DHStrProjectUrl: return deskhub::ui::kProjectUrl;
        case DHStrProjectLinkLabel: return deskhub::ui::kProjectLinkLabel;
        case DHStrAllowControlLabel: return deskhub::ui::kAllowControlLabel;
        case DHStrRequestControlLabel: return deskhub::ui::kRequestControlLabel;
        case DHStrClientIpPlaceholder: return deskhub::ui::kClientIpPlaceholder;
        case DHStrConnectPromptTitle: return deskhub::ui::kConnectPromptTitle;
        case DHStrPickDisplaysHint: return deskhub::ui::kPickDisplaysHint;
        case DHStrNoDisplayTicked: return deskhub::ui::kNoDisplayTicked;
        case DHStrStopSelectedDisplay: return deskhub::ui::kStopSelectedDisplay;
        case DHStrDisconnectSelectedViewer: return deskhub::ui::kDisconnectSelectedViewer;
        case DHStrShareStateOn: return deskhub::ui::kShareStateOn;
        case DHStrShareStateOff: return deskhub::ui::kShareStateOff;
        case DHStrStartSharing: return deskhub::ui::kStartSharing;
        case DHStrStartingShare: return deskhub::ui::kStartingShare;
        case DHStrRefreshNow: return deskhub::ui::kRefreshNow;
        case DHStrStopDisplayAction: return deskhub::ui::kStopDisplayAction;
        case DHStrDisconnectViewerAction: return deskhub::ui::kDisconnectViewerAction;
        case DHStrLanDevicesHint: return deskhub::ui::kLanDevicesHint;
        case DHStrNotSharing: return deskhub::ui::kNotSharing;
        case DHStrLanDevicesEmpty: return deskhub::ui::kLanDevicesEmpty;
        case DHStrBroadcastMemoryLabel: return deskhub::ui::kBroadcastMemoryLabel;
        case DHStrBindInterfaceLabel: return deskhub::ui::kBindInterfaceLabel;
        case DHStrBindAllInterfaces: return deskhub::ui::kBindAllInterfaces;
        case DHStrAutostartLabel: return deskhub::ui::kAutostartLabel;
        case DHStrAutoShareLabel: return deskhub::ui::kAutoShareLabel;
        case DHStrClipboardSyncLabel: return deskhub::ui::kClipboardSyncLabel;
        case DHStrCloseToTrayLabel: return deskhub::ui::kCloseToTrayLabel;
        case DHStrTrayShowWindow: return deskhub::ui::kTrayShowWindow;
        case DHStrTrayHideWindow: return deskhub::ui::kTrayHideWindow;
        case DHStrTrayQuit: return deskhub::ui::kTrayQuit;
        case DHStrBindNotConnectedNote: return deskhub::ui::kBindNotConnectedNote;
        case DHStrShareClampWarning: {
            static const std::string warning = deskhub::ui::ShareClampWarning();
            return warning.c_str();
        }
        case DHStrUdpPortLine: {
            static const std::string line = deskhub::ui::UdpPortLine();
            return line.c_str();
        }
        case DHStrSessionEnded: return deskhub::ui::kSessionEnded;
        case DHStrShareStartFailed: return deskhub::ui::kShareStartFailed;
        case DHStrScreenRecordingRequired: return deskhub::ui::kScreenRecordingRequired;
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

int dh_could_not_connect(const char* address, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity),
        deskhub::ui::CouldNotConnectTo(address ? address : ""));
    return int(std::strlen(out));
}

int dh_source_query_failed(const char* address, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity),
        deskhub::ui::SourceQueryFailed(address ? address : ""));
    return int(std::strlen(out));
}

int dh_source_query_empty(const char* address, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity),
        deskhub::ui::SourceQueryEmpty(address ? address : ""));
    return int(std::strlen(out));
}

int dh_udp_port_line(uint32_t port, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity), deskhub::ui::UdpPortLine(uint16_t(port)));
    return int(std::strlen(out));
}

int dh_compose_address(const char* host, const char* portText, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const uint16_t port = deskhub::ui::PortOrDefault(portText ? portText : "");
    deskhubp::CopyToBuf(out, size_t(capacity), deskhub::ui::AddressWithPort(host ? host : "", port));
    return int(std::strlen(out));
}

int dh_address_host(const char* address, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity), deskhub::ui::AddressHost(address ? address : ""));
    return int(std::strlen(out));
}

uint32_t dh_address_port(const char* address) {
    return deskhub::ui::AddressPort(address ? address : "");
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

bool dh_is_valid_passcode(const char* passcode) {
    return passcode && deskhub::IsValidPasscode(passcode);
}

int dh_passcode_digits(void) {
    return int(deskhub::kPasscodeDigits);
}

int dh_max_sources(void) {
    return int(deskhub::kMaxSources);
}

int dh_list_sources(const char* address, DHSourceInfo* out, int capacity, const char* passcode) {
    if (!address || !out || capacity <= 0) return DH_SOURCE_QUERY_FAILED;

    NetAddr server;
    if (!ParseNetAddr(address, server)) {
        LOGE("[Bridge] Invalid address: %s", address);
        return DH_SOURCE_QUERY_FAILED;
    }

    std::vector<deskhub::SourceInfo> sources;
    if (!QuerySources(server, sources, passcode ? passcode : ""))
        return DH_SOURCE_QUERY_FAILED;

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

bool dh_connect_decision(const DHSourceInfo* sources, int count, uint8_t* out_source_id) {
    std::vector<deskhub::SourceInfo> list;
    if (sources && count > 0) {
        list.resize(size_t(count));
        for (int i = 0; i < count; ++i) list[size_t(i)].sourceId = sources[i].sourceId;
    }
    const deskhub::ConnectDecision d = deskhub::DecideAfterSourceQuery(list);
    if (out_source_id) *out_source_id = d.sourceId;
    return d.showPicker;
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

int32_t dh_scroll_notches_from_lines(double lines) {
    return deskhub::ScrollNotchesFromLines(lines);
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

bool dh_native_key_to_vk(int32_t native_key_code, int32_t* out_vk, int32_t* out_scan) {
    if (!out_vk || !out_scan) return false;
    return deskhubp::NativeKeyToWin(native_key_code, *out_vk, *out_scan);
}

int32_t dh_vk_scancode(int32_t vk) {
    return deskhub::VkToSet1Scancode(vk);
}

bool dh_is_lock_toggle_vk(int32_t vk) {
    return vk == deskhub::kViewerLockToggleVk;
}

bool dh_is_escape_vk(int32_t vk) {
    return vk == deskhub::kVkEscape;
}

DHPointerLockEffect dh_pointer_toggle_lock(DHPointerLock* state) {
    return ApplyPointerLock(state, [](deskhub::PointerLockState& s) { return s.OnToggleLockKey(); });
}

DHPointerLockEffect dh_pointer_escape(DHPointerLock* state) {
    return ApplyPointerLock(state, [](deskhub::PointerLockState& s) { return s.OnEscape(); });
}

DHPointerLockEffect dh_pointer_focus_lost(DHPointerLock* state) {
    return ApplyPointerLock(state, [](deskhub::PointerLockState& s) { return s.OnFocusLost(); });
}

int dh_pointer_subtitle(DHPointerLock state, const char* statusLine, char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const deskhub::PointerLockState s(state.locked);
    deskhubp::CopyToBuf(out, size_t(capacity), s.SubtitleFor(statusLine ? statusLine : ""));
    return int(std::strlen(out));
}

void dh_set_data_dir(const char* dir) {
    deskhubp::SetAppDataDir(dir ? std::string(dir) : std::string());
}

void dh_viewer_opened() {
    OpenViewers().Opened();
}

bool dh_viewer_closed() {
    return OpenViewers().Closed();
}
}
