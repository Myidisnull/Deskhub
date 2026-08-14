#include "deskhub/ui/AutostartConfig.h"

#include <string>

namespace deskhub::ui {
namespace {

std::string QuotedSchtasksName(const char* name) {
    const std::string text = name ? name : "";
    if (text.find(' ') == std::string::npos) return text;
    return "\"" + text + "\"";
}

}

std::string BuildXdgAutostartEntry(const std::string& execPath) {
    return std::string("[Desktop Entry]\n") +
           "Type=Application\n"
           "Name=" +
           brand::kProductName +
           "\n"
           "Comment=Share and control desktops over the local network\n"
           "Exec=" +
           execPath +
           "\n"
           "Icon=deskhub\n"
           "Terminal=false\n"
           "X-GNOME-Autostart-enabled=true\n";
}

std::string BuildSchtasksCreateArgs(const std::string& exePath) {
    return std::string("/Create /F /TN ") + QuotedSchtasksName(kAutostartTaskName) +
           " /SC ONLOGON /RL HIGHEST /TR \"\\\"" + exePath + "\\\"\"";
}

std::string BuildSchtasksQueryArgs() {
    return std::string("/Query /TN ") + QuotedSchtasksName(kAutostartTaskName);
}

std::string BuildSchtasksDeleteArgs() {
    return std::string("/Delete /F /TN ") + QuotedSchtasksName(kAutostartTaskName);
}

}
