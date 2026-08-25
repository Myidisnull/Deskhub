#pragma once
#include "deskhub/ui/Brand.h"
#include "deskhub/ui/Locale.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/LinkPulse.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#ifndef DESKHUB_VERSION
#define DESKHUB_VERSION "0.0-dev"
#endif

namespace deskhub::ui {

inline constexpr const char* kAppVersion = DESKHUB_VERSION;
inline constexpr const char* kProjectUrl = brand::kProjectUrl;
inline constexpr LStr kProjectLinkLabel{"GitHub"};
inline constexpr LStr kAppTitle{"{app}"};
inline constexpr LStr kAppDescription{"Important services for {app}"};
inline constexpr LStr kHostIpIntro{"Others connect to you using one of these IP addresses:"};
inline constexpr LStr kNoNetworkAddress{"(no network address found)"};
inline constexpr LStr kClientIpPrompt{"Host machine IP address:"};
inline constexpr LStr kPickerTitle{"What do you want to view?"};
inline constexpr LStr kPickerEachWindow{"Each one you pick opens its own window."};
inline constexpr LStr kShareButton{"Share...  (pick the display to share)"};
inline constexpr LStr kSharingTitle{"{app} - sharing"};
inline constexpr LStr kSharingSourcesIntro{"Sources currently being shared:"};
inline constexpr LStr kSharingConnectHint{"Others connect by entering this machine's IP address."};
inline constexpr LStr kNothingShared{"(nothing is being shared)"};
inline constexpr LStr kStopSharing{"Stop sharing"};
inline constexpr LStr kShareStartFailed{"Could not start sharing"};
inline constexpr LStr kQueryingSources{"Asking the other machine what it is sharing..."};
inline constexpr LStr kConnectInProgress{"A connection attempt is already in progress."};
inline constexpr LStr kReconnecting{"Reconnecting\xE2\x80\xA6"};
inline constexpr LStr kViewerOpenFailed{
    "Could not open a viewing session - check the address, that the other machine is sharing, "
    "and that this machine has a usable GPU (D3D11)."};
inline constexpr LStr kViewerGpuFailed{
    "Could not open a viewing session: this machine has no usable D3D11 GPU "
    "(common on virtual machines without 3D acceleration / GPU passthrough)."};
inline constexpr LStr kConnectionEndedTitle{"Connection ended"};
inline constexpr LStr kDisconnected{"disconnected"};
inline constexpr LStr kSessionEnded{"Session ended"};
inline constexpr LStr kScreenRecordingRequired{
    "Screen Recording permission is required. Grant it in System Settings, then quit and "
    "reopen {app}."};
inline constexpr LStr kSidebarHost{"Host"};
inline constexpr LStr kSidebarClient{"Client"};
inline constexpr LStr kSidebarSettings{"Settings"};
inline constexpr LStr kHostHeading{"Share this machine's screen"};
inline constexpr LStr kClientHeading{"Connect to another machine"};
inline constexpr LStr kSettingsHeading{"Share settings"};
inline constexpr LStr kSettingsHint{"These apply the next time you start sharing."};
inline constexpr LStr kClientSettingsHeading{"Connection settings"};
inline constexpr LStr kClientSettingsHint{
    "The device scan looks for sharing machines on this UDP port. Match it to the port in the "
    "host's Share settings."};
inline constexpr LStr kRecentDevicesHeading{"Recent devices"};
inline constexpr LStr kRecentDevicesHint{"Click a device to connect to it again."};
inline constexpr LStr kRecentDevicesEmpty{"Devices you connect to will appear here."};
inline constexpr LStr kForgetDevice{"Forget"};
inline constexpr LStr kStatusOnline{"Online"};
inline constexpr LStr kStatusOffline{"Offline"};
inline constexpr LStr kStatusChecking{"Checking..."};
inline constexpr LStr kNotSharing{"Not sharing."};
inline constexpr LStr kStartingShare{"Starting share..."};
inline constexpr LStr kShareStateOn{"Sharing"};
inline constexpr LStr kShareStateOff{"Not sharing"};
inline constexpr LStr kStartSharing{"Start sharing"};
inline constexpr LStr kBroadcastMemoryLabel{"Broadcast memory"};
inline constexpr LStr kAllowControlLabel{"Viewers can control this machine (mouse and keyboard)"};
inline constexpr LStr kShareOnLaunchLabel{"Start sharing automatically when the app launches"};
inline constexpr LStr kRunInBackgroundLabel{"Keep running in the background when the window is closed"};
inline constexpr LStr kHideTrayIconLabel{"Hide the tray / menu bar icon"};
inline constexpr LStr kLogMaxFileMbLabel{"Split log when larger than (MB)"};
inline constexpr LStr kLogCompressAfterDaysLabel{"Compress logs older than (days, 0 = never)"};
inline constexpr LStr kLogDeleteAfterDaysLabel{"Delete logs older than (days, 0 = never)"};
inline constexpr LStr kLogDirLabel{"Log directory"};
inline constexpr LStr kLogDirHint{"Leave blank to use the default {app} folder. New logs go here; existing files stay put."};
inline constexpr LStr kLogDirInvalid{"That log directory cannot be used. Choose an absolute, writable folder."};
inline constexpr LStr kLogDirBrowse{"Browse\xE2\x80\xA6"};
inline constexpr LStr kLogDetailsLabel{"Log details"};
inline constexpr LStr kLogRefresh{"Refresh"};
inline constexpr LStr kLogOpenFolder{"Open log folder"};
inline constexpr LStr kLogEmpty{"No log files yet."};
inline constexpr LStr kBackgroundPromptTitle{"Close {app}"};
inline constexpr LStr kBackgroundPromptMessage{"Keep running in the background after closing?"};
inline constexpr LStr kBackgroundPromptYes{"Yes"};
inline constexpr LStr kBackgroundPromptNo{"No"};
inline constexpr LStr kBackgroundPromptConfirm{"Confirm"};
inline constexpr LStr kBackgroundPromptClose{"Close"};
inline constexpr LStr kTrayRestore{"Restore"};
inline constexpr LStr kTrayExit{"Exit"};
inline constexpr LStr kBackgroundRunningHint{"{app} is still running in the background."};
inline constexpr LStr kAlreadyRunning{"{app} is already running. Close the other window or use the tray icon, then try again."};
inline constexpr LStr kQuitWhileBusyMessage{"Sharing or a connection is still active. Quit {app} anyway?"};
inline constexpr LStr kQuitWhileBusyQuit{"Quit"};
inline constexpr LStr kQuitWhileBusyCancel{"Cancel"};
inline constexpr LStr kRequestControlLabel{"Control the remote machine (untick to just watch)"};
inline constexpr LStr kViewOnlyNote{"View-only: viewers can watch but not control."};
inline constexpr LStr kPickDisplaysHint{"Tick the displays to share, then press Share."};
inline constexpr LStr kPickDisplaysPortalHint{"Press Share, then pick the displays in your desktop's screen-sharing dialog."};
inline constexpr LStr kNoDisplayTicked{"Tick at least one display to share."};
inline constexpr LStr kNoDisplayFound{"No display found to share."};
inline constexpr LStr kWaitingForDisplays{
    "No display yet \xE2\x80\x94 waiting for the desktop to finish starting before sharing"
    "\xE2\x80\xA6"};
inline constexpr LStr kStopSelectedDisplay{"Stop selected display"};
inline constexpr LStr kDisconnectSelectedViewer{"Disconnect selected viewer"};
inline constexpr LStr kStopDisplayAction{"Stop"};
inline constexpr LStr kDisconnectViewerAction{"Disconnect"};
inline constexpr LStr kDisconnectButton{"Disconnect"};
inline constexpr LStr kPasscodeLabel{"Passcode (4 digits, required)"};
inline constexpr LStr kClientPasscodePrompt{"Passcode (4 digits):"};
inline constexpr LStr kClientPasscodeHint{"Read the 4-digit code off the host."};
inline constexpr LStr kDeviceNameLabel{"Your name"};
inline constexpr LStr kClientIpPlaceholder{"192.168.1.10"};
inline constexpr LStr kUdpPortLabel{"UDP port"};
inline constexpr LStr kBindInterfaceLabel{"Share on network"};
inline constexpr LStr kBindAllInterfaces{"All networks"};
inline constexpr LStr kBindNotConnectedNote{"not connected"};
inline constexpr LStr kSettingsSectionVideo{"Video"};
inline constexpr LStr kSettingsSectionConnection{"Connection"};
inline constexpr LStr kSettingsSectionSecurity{"Security"};
inline constexpr LStr kSettingsSectionSession{"Session"};
inline constexpr LStr kSettingsSectionLaunch{"Launch & background"};
inline constexpr LStr kAutostartLabel{"Start {app} when you log in"};
inline constexpr LStr kAutoShareLabel = kShareOnLaunchLabel;
inline constexpr LStr kClipboardSyncLabel{"Sync clipboard text with connected devices"};
inline constexpr LStr kShareAudioLabel{"Share this device's sound with viewers"};
inline constexpr LStr kAcceptFilesLabel{"Accept files sent by connected viewers"};
inline constexpr LStr kSendFilesLabel{"Send files to this device"};
inline constexpr LStr kOpenDesktopLabel{"Open desktop"};
inline constexpr LStr kOpenFilesLabel{"Send files"};
inline constexpr LStr kOpenShellLabel{"Open shell"};
inline constexpr LStr kShareTerminalLabel{"Share a shell with connected viewers"};
inline constexpr LStr kTerminalCloseButton{"Close"};
inline constexpr LStr kTerminalConnecting{"Opening shell\xE2\x80\xA6"};
inline constexpr LStr kTerminalConnected{"Shell connected"};
inline constexpr LStr kTerminalReattached{"Shell reattached"};
inline constexpr LStr kTerminalReattaching{"Reattaching shell\xE2\x80\xA6"};
inline constexpr LStr kTerminalClosed{"Shell closed"};
inline constexpr LStr kTerminalUnreachable{"Could not reach the shell host."};
inline constexpr LStr kTerminalRefusedPasscode{"Wrong passcode for shell."};
inline constexpr LStr kTerminalRefusedBusy{"Too many shells are open on the host."};
inline constexpr LStr kTerminalRefusedOff{"The host is not sharing a shell."};
inline constexpr LStr kTerminalRefusedGone{"That shell session is gone."};
inline constexpr LStr kTerminalRefused{"Shell refused."};

inline constexpr LStr kPairedHeading{"Machines allowed to connect to this one"};
inline constexpr LStr kPairedHint{
    "A machine gets on this list once, and after that it is recognised by its key \xE2\x80\x94 "
    "no passcode is asked for again."};
inline constexpr LStr kPairedEmpty{"(no machine has paired with this one yet)"};
inline constexpr LStr kPairedForget{"Forget"};
inline constexpr LStr kPairedForgetAll{"Forget every machine"};
inline constexpr LStr kPairedForgetAllPrompt{
    "Every machine will have to pair again before it can connect. Continue?"};
inline constexpr LStr kPairedForgetNote{
    "Changing the passcode does NOT turn these machines away \xE2\x80\x94 they no longer use it. "
    "Forgetting them is what does."};
inline constexpr LStr kAllowPairingLabel{"Let new machines pair with this one"};
inline constexpr LStr kAllowPairingHint{
    "Turn this off once your own machines are paired: a passcode that leaks is then worth "
    "nothing, and the machines already on the list keep working."};
inline constexpr LStr kThisMachineHeading{"This machine's key"};
inline constexpr LStr kThisMachineHint{
    "Read this out over the phone to whoever is connecting. It is the one thing a machine in "
    "the middle cannot fake."};
inline constexpr LStr kPairedColumnName{"Machine"};
inline constexpr LStr kPairedColumnKey{"Key"};
inline constexpr LStr kPairedColumnPaired{"Paired"};
inline constexpr LStr kPairedColumnLastSeen{"Last seen"};
inline constexpr LStr kPairingRequestTitle{"Let this machine in?"};
inline constexpr LStr kPairingAllow{"Allow"};
inline constexpr LStr kPairingDeny{"Deny"};

inline std::string PairingRequestBody(std::string_view name, std::string_view address,
    std::string_view shortKey) {
    std::string out(name.empty() ? std::string("A machine") : std::string(name));
    out += " at ";
    out += address;
    out += " wants to pair (key ";
    out += shortKey;
    out += ").";
    return out;
}

inline std::string TerminalRefusalText(TermReason reason) {
    switch (reason) {
        case TermReason::WrongPasscode: return std::string(kTerminalRefusedPasscode);
        case TermReason::TooManySessions: return std::string(kTerminalRefusedBusy);
        case TermReason::NotShared: return std::string(kTerminalRefusedOff);
        case TermReason::NoSuchSession: return std::string(kTerminalRefusedGone);
        case TermReason::Accepted: break;
    }
    return std::string(kTerminalRefused);
}

inline constexpr LStr kConnectedPickSession{"Connected \xE2\x80\x94 choose what to open."};
inline constexpr LStr kPlayAudioLabel{"Play the sound of the device you are watching"};
inline constexpr LStr kKeepAwakeLabel{"Keep this device awake while a session is active"};
inline constexpr LStr kEncryptSessionLabel{"Encrypt session traffic"};
inline constexpr LStr kEncryptSessionHint{
    "When on, session video, input and clipboard are encrypted. Discovery stays cleartext. "
    "Copy the session key to viewers unless escrow is on."};
inline constexpr LStr kSessionKeyLabel{"Session key"};
inline constexpr LStr kSessionKeyHint{"Copy this key to viewers when escrow is off."};
inline constexpr LStr kCopySessionKey{"Copy key"};
inline constexpr LStr kCopy{"Copy"};
inline constexpr LStr kCopied{"Copied"};
inline constexpr LStr kRefreshSessionKey{"Refresh key"};
inline constexpr LStr kEscrowSessionKeyLabel{"Escrow key to viewers"};
inline constexpr LStr kEscrowSessionKeyHint{
    "When on, viewers that know the passcode receive the session key automatically."};
inline constexpr LStr kSessionKeyLifetimeLabel{"Key lifetime"};
inline constexpr LStr kSessionKeyLifetimePerShare{"Per share"};
inline constexpr LStr kSessionKeyLifetimePersistent{"Persistent"};
inline constexpr LStr kClientSessionKeyPrompt{"Session key:"};
inline constexpr LStr kClientSessionKeyHint{
    "Required when the host encrypts without escrow. Paste the key from the host."};
inline constexpr LStr kSessionKeyInvalid{"Enter the session key shown on the host."};
inline constexpr LStr kCloseToTrayLabel = kRunInBackgroundLabel;
inline constexpr LStr kTrayShowWindow{"Show {app}"};
inline constexpr LStr kTrayHideWindow{"Hide window"};
inline constexpr LStr kTrayQuit{"Quit {app}"};
inline constexpr LStr kLanDevicesHeading{"Machines sharing on this network"};
inline constexpr LStr kLanDevicesEmpty{"Looking for devices that are sharing\xE2\x80\xA6"};
inline constexpr LStr kLanDevicesHint{"Click a device to connect to it."};
inline constexpr LStr kLanDevicesNoneSharing{
    "A machine appears here only while it is sharing \xE2\x80\x94 start the share on it, then "
    "check again."};
inline constexpr LStr kScanRescanNote{"Checking again shortly."};
inline constexpr LStr kRefreshNow{"Refresh now"};
inline constexpr LStr kScanNoLocalNetwork{"This machine has no network address to scan from."};
inline constexpr LStr kConnectPromptTitle{"Connect to this device"};
inline constexpr LStr kPasscodeInvalid{"The passcode must be exactly 4 digits (for example 0417)."};

inline constexpr LStr kLanguageLabel{"Language"};
inline constexpr LStr kLanguageSystem{"System default"};
inline constexpr LStr kSettingsSectionLanguage{"Language"};
inline constexpr LStr kLanguageRestartHint{"The new language applies after you restart the app."};

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

inline constexpr LStr kLinkQualityGood{"Good"};
inline constexpr LStr kLinkQualityFair{"Fair"};
inline constexpr LStr kLinkQualityPoor{"Poor"};
inline constexpr LStr kLinkNoReading{"\xE2\x80\x94"};

inline const char* LinkQualityText(LinkQuality quality) {
    switch (quality) {
        case LinkQuality::Good: return kLinkQualityGood.get();
        case LinkQuality::Fair: return kLinkQualityFair.get();
        case LinkQuality::Poor: return kLinkQualityPoor.get();
        case LinkQuality::Unknown: break;
    }
    return kLinkNoReading.get();
}

inline std::string LinkPingText(bool haveRtt, uint32_t rttUs) {
    if (!haveRtt) return std::string(kLinkNoReading.get());
    return PingMs((rttUs + 500) / 1000);
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
    if (total == 0) return std::string(kScanNoLocalNetwork);
    const char* detail = found > 0 ? kLanDevicesHint.get() : kLanDevicesNoneSharing.get();
    return ScanFinishedStatus(found, total) + " " + detail + " " + ScanRecheckNote(rescanSecs);
}

inline std::string RecentDevicesNote(size_t deviceCount, uint32_t recheckSecs) {
    if (deviceCount == 0) return std::string(kRecentDevicesEmpty);
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
