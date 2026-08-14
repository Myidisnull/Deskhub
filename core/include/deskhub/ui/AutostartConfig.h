#pragma once
#include "deskhub/ui/Brand.h"

#include <string>

namespace deskhub::ui {

inline constexpr const char* kAutostartTaskName = brand::kAutostartTaskId;
inline constexpr const char* kAutostartDesktopFileName = brand::kAutostartDesktopFile;

std::string BuildXdgAutostartEntry(const std::string& execPath);
std::string BuildSchtasksCreateArgs(const std::string& exePath);
std::string BuildSchtasksQueryArgs();
std::string BuildSchtasksDeleteArgs();

}
