#pragma once
#include <string>

namespace deskhub::ui {

inline constexpr const char* kAutostartTaskName = "Deskhub";
inline constexpr const char* kAutostartDesktopFileName = "deskhub.desktop";

std::string BuildXdgAutostartEntry(const std::string& execPath);
std::string BuildSchtasksCreateArgs(const std::string& exePath);
std::string BuildSchtasksQueryArgs();
std::string BuildSchtasksDeleteArgs();

}
