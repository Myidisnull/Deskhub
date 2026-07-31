#pragma once
#include <string>
#include <string_view>

namespace deskhub {

inline constexpr std::string_view kViewerConnectingStatus = "connecting...";
inline constexpr std::string_view kViewerLockHint = "Press F9 to lock mouse";

inline std::string ComposeViewerTitle(std::string_view base, std::string_view statusLine,
    std::string_view hint) {
    std::string title(base);
    title += " \xE2\x80\x94 ";
    title += statusLine.empty() ? kViewerConnectingStatus : statusLine;
    title += " \xC2\xB7 ";
    title += hint;
    return title;
}

}
