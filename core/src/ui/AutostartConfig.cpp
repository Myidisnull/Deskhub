#include "deskhub/ui/AutostartConfig.h"

namespace deskhub::ui {

std::string BuildXdgAutostartEntry(const std::string& execPath) {
    return std::string("[Desktop Entry]\n") +
           "Type=Application\n"
           "Name=" +
           kAutostartTaskName +
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
    return std::string("/Create /F /TN ") + kAutostartTaskName +
           " /SC ONLOGON /RL HIGHEST /TR \"\\\"" + exePath + "\\\"\"";
}

std::string BuildSchtasksQueryArgs() {
    return std::string("/Query /TN ") + kAutostartTaskName;
}

std::string BuildSchtasksDeleteArgs() {
    return std::string("/Delete /F /TN ") + kAutostartTaskName;
}

}
