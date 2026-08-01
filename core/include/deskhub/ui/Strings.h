#pragma once
#include "deskhub/protocol/Wire.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace deskhub::ui {

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
inline constexpr const char* kQueryingSources =
    "Asking the other machine what it is sharing...";
inline constexpr const char* kViewerOpenFailed =
    "Could not open a viewing session - check the address and that the other machine is "
    "sharing.";
inline constexpr const char* kConnectionEndedTitle = "Connection ended";
inline constexpr const char* kDisconnected = "disconnected";
inline constexpr const char* kSessionEnded = "Session ended";

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

inline std::string UdpPortLine() {
    return "UDP port " + std::to_string(kDeskhubPort);
}

inline std::string InvalidAddressHint() {
    return "Enter just the IP address (e.g., 192.168.1.10). Deskhub always uses UDP port " +
           std::to_string(kDeskhubPort) + ".";
}

}
