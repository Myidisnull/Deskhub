#pragma once
#include "deskhub/protocol/Wire.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#ifndef DESKHUB_VERSION
#define DESKHUB_VERSION "0.0-dev"
#endif

namespace deskhub::ui {

inline constexpr const char* kAppVersion = DESKHUB_VERSION;
inline constexpr const char* kProjectUrl = "https://github.com/manhpham90vn/Deskhub";
inline constexpr const char* kProjectLinkLabel = "GitHub";
inline constexpr const char* kAppTitle = "Deskhub - stream & remotely control an application";
inline constexpr const char* kHostIpIntro =
    "Others connect to you using one of these IP addresses:";
inline constexpr const char* kNoNetworkAddress = "(no network address found)";
inline constexpr const char* kClientIpPrompt = "Host machine IP address:";
inline constexpr const char* kPickerTitle = "What do you want to view?";
inline constexpr const char* kPickerEachWindow = "Each one you pick opens its own window.";
inline constexpr const char* kShareButton = "Share...  (pick the display to share)";
inline constexpr const char* kSharingTitle = "Deskhub - sharing";
inline constexpr const char* kSharingSourcesIntro = "Sources currently being shared:";
inline constexpr const char* kSharingConnectHint =
    "Others connect by entering this machine's IP address.";
inline constexpr const char* kNothingShared = "(nothing is being shared)";
inline constexpr const char* kStopSharing = "Stop sharing";
inline constexpr const char* kShareStartFailed = "Could not start sharing";
inline constexpr const char* kQueryingSources =
    "Asking the other machine what it is sharing...";
inline constexpr const char* kViewerOpenFailed =
    "Could not open a viewing session - check the address and that the other machine is "
    "sharing.";
inline constexpr const char* kConnectionEndedTitle = "Connection ended";
inline constexpr const char* kDisconnected = "disconnected";
inline constexpr const char* kSessionEnded = "Session ended";
inline constexpr const char* kScreenRecordingRequired =
    "Screen Recording permission is required. Grant it in System Settings, then quit and "
    "reopen Deskhub.";
inline constexpr const char* kSidebarHost = "Host";
inline constexpr const char* kSidebarClient = "Client";
inline constexpr const char* kSidebarSettings = "Settings";
inline constexpr const char* kHostHeading = "Share this machine's screen";
inline constexpr const char* kClientHeading = "Connect to another machine";
inline constexpr const char* kSettingsHeading = "Share settings";
inline constexpr const char* kSettingsHint = "These apply the next time you start sharing.";
inline constexpr const char* kClientSettingsHeading = "Connection settings";
inline constexpr const char* kClientSettingsHint =
    "The device scan looks for sharing machines on this UDP port. Match it to the port in the "
    "host's Share settings.";
inline constexpr const char* kRecentDevicesHeading = "Recent devices";
inline constexpr const char* kRecentDevicesHint = "Click a device to connect to it again.";
inline constexpr const char* kRecentDevicesEmpty = "Devices you connect to will appear here.";
inline constexpr const char* kStatusOnline = "Online";
inline constexpr const char* kStatusOffline = "Offline";
inline constexpr const char* kStatusChecking = "Checking...";
inline constexpr const char* kNotSharing = "Not sharing.";
inline constexpr const char* kStartingShare = "Starting share...";
inline constexpr const char* kShareStateOn = "Sharing";
inline constexpr const char* kShareStateOff = "Not sharing";
inline constexpr const char* kStartSharing = "Start sharing";
inline constexpr const char* kBroadcastMemoryLabel = "Broadcast memory";
inline constexpr const char* kAllowControlLabel =
    "Viewers can control this machine (mouse and keyboard)";
inline constexpr const char* kRequestControlLabel =
    "Control the remote machine (untick to just watch)";
inline constexpr const char* kViewOnlyNote = "View-only: viewers can watch but not control.";
inline constexpr const char* kPickDisplaysHint = "Tick the displays to share, then press Share.";
inline constexpr const char* kPickDisplaysPortalHint =
    "Press Share, then pick the displays in your desktop's screen-sharing dialog.";
inline constexpr const char* kNoDisplayTicked = "Tick at least one display to share.";
inline constexpr const char* kStopSelectedDisplay = "Stop selected display";
inline constexpr const char* kDisconnectSelectedViewer = "Disconnect selected viewer";
inline constexpr const char* kStopDisplayAction = "Stop";
inline constexpr const char* kDisconnectViewerAction = "Disconnect";
inline constexpr const char* kPasscodeLabel = "Passcode (4 digits, required)";
inline constexpr const char* kClientPasscodePrompt = "Passcode (4 digits):";
inline constexpr const char* kClientPasscodeHint = "Read the 4-digit code off the host.";
inline constexpr const char* kDeviceNameLabel = "Your name";
inline constexpr const char* kClientIpPlaceholder = "192.168.1.10";
inline constexpr const char* kUdpPortLabel = "UDP port";
inline constexpr const char* kBindInterfaceLabel = "Share on network";
inline constexpr const char* kBindAllInterfaces = "All networks";
inline constexpr const char* kBindNotConnectedNote = "not connected";
inline constexpr const char* kSettingsSectionVideo = "Video";
inline constexpr const char* kSettingsSectionConnection = "Connection";
inline constexpr const char* kSettingsSectionSecurity = "Security";
inline constexpr const char* kSettingsSectionSession = "Session";
inline constexpr const char* kSettingsSectionLaunch = "Launch & background";
inline constexpr const char* kAutostartLabel = "Start Deskhub when you log in";
inline constexpr const char* kAutoShareLabel = "Start sharing when Deskhub opens";
inline constexpr const char* kClipboardSyncLabel =
    "Sync clipboard text with connected devices";
inline constexpr const char* kCloseToTrayLabel =
    "Keep running in the background (tray icon) when the window is closed";
inline constexpr const char* kTrayShowWindow = "Show Deskhub";
inline constexpr const char* kTrayHideWindow = "Hide window";
inline constexpr const char* kTrayQuit = "Quit Deskhub";
inline constexpr const char* kLanDevicesHeading = "Machines sharing on this network";
inline constexpr const char* kLanDevicesEmpty = "Looking for devices that are sharing\xE2\x80\xA6";
inline constexpr const char* kLanDevicesHint = "Click a device to connect to it.";
inline constexpr const char* kLanDevicesNoneSharing =
    "A machine appears here only while it is sharing \xE2\x80\x94 phones and tablets can view "
    "but never share.";
inline constexpr const char* kScanRescanNote = "Checking again shortly.";
inline constexpr const char* kRefreshNow = "Refresh now";
inline constexpr const char* kScanNoLocalNetwork =
    "This machine has no network address to scan from.";
inline constexpr const char* kConnectPromptTitle = "Connect to this device";
inline constexpr const char* kPasscodeInvalid =
    "The passcode must be exactly 4 digits (for example 0417).";

inline std::string TrimAscii(std::string_view s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

inline uint32_t ParsePositiveUint(std::string_view s, uint32_t fallback) {
    if (s.empty()) return fallback;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return fallback;
        v = v * 10 + uint64_t(c - '0');
        if (v > 0xFFFFFFFFull) return fallback;
    }
    return v > 0 ? uint32_t(v) : fallback;
}

inline std::string ShareClampWarning() {
    const std::string cap = std::to_string(kMaxSources);
    return "This machine has more than " + cap + " displays. Only the first " + cap +
           " will be shared.";
}

inline std::string BindFallbackWarning(std::string_view requested) {
    return "Network " + std::string(requested) +
           " was not found - sharing on all networks instead.";
}

inline std::string ConnectingTo(std::string_view address) {
    return "Connecting to " + std::string(address) + "\xE2\x80\xA6";
}

inline std::string HostTitle(std::string_view address, uint32_t width, uint32_t height) {
    std::string title(address);
    if (!width || !height) return title;
    title += " \xE2\x80\x94 ";
    title += std::to_string(width);
    title += "\xC3\x97";
    title += std::to_string(height);
    return title;
}

inline bool SplitHostPort(std::string_view s, std::string& host, uint16_t& port) {
    const size_t colon = s.find(':');
    if (colon == std::string_view::npos) {
        if (s.empty()) return false;
        host = std::string(s);
        return true;
    }

    const std::string_view hostPart = s.substr(0, colon);
    const std::string_view portPart = s.substr(colon + 1);
    if (hostPart.empty() || portPart.empty()) return false;

    uint32_t value = 0;
    for (char c : portPart) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + uint32_t(c - '0');
        if (value > 65535) return false;
    }
    if (value == 0) return false;

    host = std::string(hostPart);
    port = uint16_t(value);
    return true;
}

inline std::string VersionLine() {
    return std::string("Version ") + kAppVersion;
}

inline std::string UdpPortLine(uint16_t port) {
    return "UDP port " + std::to_string(port);
}

inline std::string UdpPortLine() {
    return UdpPortLine(kDeskhubPort);
}

inline std::string PingMs(uint32_t ms) {
    return std::to_string(ms) + " ms";
}

inline std::string SharingStatusLine(uint16_t port) {
    return "Sharing on UDP port " + std::to_string(port) +
           " - others can connect to this machine now.";
}

inline std::string PasscodeNote(std::string_view passcode) {
    return "Viewers need passcode " + std::string(passcode) + ".";
}

inline std::string CouldNotConnectTo(std::string_view address) {
    return "Could not connect to " + std::string(address) + ".";
}

inline std::string SourceQueryFailed(std::string_view address) {
    return "No reply from " + std::string(address) +
           " - check that the other machine is sharing and that the passcode matches.";
}

inline std::string SourceQueryEmpty(std::string_view address) {
    return std::string(address) +
           " replied without any sources - check the 4-digit passcode on the host, and that it "
           "is still sharing.";
}

inline std::string ScanningStatus(size_t probed, size_t total, uint16_t port) {
    return "Looking for hosts on " + UdpPortLine(port) + " - " + std::to_string(probed) + " of " +
           std::to_string(total) + " addresses checked" + "\xE2\x80\xA6";
}

inline std::string ScanRecheckNote(uint32_t seconds) {
    return "Checking again in " + std::to_string(seconds) + "s.";
}

inline std::string StatusRecheckNote(uint32_t seconds) {
    return "Status and ping recheck every " + std::to_string(seconds) + "s.";
}

inline std::string ScanFinishedStatus(size_t found, size_t total) {
    return std::to_string(found) + (found == 1 ? " device" : " devices") + " found after checking " +
           std::to_string(total) + " addresses.";
}

inline std::string LanDevicesNote(size_t found, size_t total, uint32_t rescanSecs) {
    if (total == 0) return kScanNoLocalNetwork;
    const char* detail = found > 0 ? kLanDevicesHint : kLanDevicesNoneSharing;
    return ScanFinishedStatus(found, total) + " " + detail + " " + ScanRecheckNote(rescanSecs);
}

inline std::string RecentDevicesNote(size_t deviceCount, uint32_t recheckSecs) {
    if (deviceCount == 0) return kRecentDevicesEmpty;
    return std::string(kRecentDevicesHint) + " " + StatusRecheckNote(recheckSecs);
}

inline uint16_t PortOrDefault(std::string_view typed) {
    const std::string trimmed = TrimAscii(typed);
    if (trimmed.empty() || trimmed.size() > 5) return kDeskhubPort;
    uint32_t value = 0;
    for (char c : trimmed) {
        if (c < '0' || c > '9') return kDeskhubPort;
        value = value * 10 + uint32_t(c - '0');
    }
    if (value < 1 || value > 65535) return kDeskhubPort;
    return uint16_t(value);
}

inline std::string AddressWithPort(std::string_view typed, uint16_t port) {
    std::string trimmed = TrimAscii(typed);
    if (trimmed.empty() || trimmed.find(':') != std::string::npos) return trimmed;
    return trimmed + ":" + std::to_string(port);
}

inline std::string AddressHost(std::string_view address) {
    std::string trimmed = TrimAscii(address);
    std::string host;
    uint16_t port = 0;
    if (SplitHostPort(trimmed, host, port)) return host;
    return trimmed;
}

inline uint16_t AddressPort(std::string_view address) {
    std::string host;
    uint16_t port = 0;
    SplitHostPort(TrimAscii(address), host, port);
    return port;
}

inline std::string InvalidAddressHint() {
    const std::string port = std::to_string(kDeskhubPort);
    return "Enter the host's IP address, with an optional port (e.g., 192.168.1.10 or "
           "192.168.1.10:" +
           port + "). The default UDP port is " + port + ".";
}

}
