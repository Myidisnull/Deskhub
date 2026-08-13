#pragma once
#include "deskhub/protocol/Wire.h"
#include "deskhub/ui/UiSettings.h"
#include "deskhubp/system/AppDataFile.h"
#include "deskhubp/system/DeviceName.h"
#include "deskhubp/system/Random.h"

namespace deskhubp {

inline constexpr const char* kUiSettingsFileName = "ui-settings.txt";

inline void SaveUiSettings(const deskhub::ui::UiSettings& settings) {
    WriteAppDataFile(kUiSettingsFileName, deskhub::ui::SerializeUiSettings(settings));
}

inline deskhub::ui::UiSettings LoadUiSettings() {
    deskhub::ui::UiSettings settings =
        deskhub::ui::ParseUiSettings(ReadAppDataFile(kUiSettingsFileName));
    if (deskhub::IsValidPasscode(settings.passcode)) return settings;

    settings.passcode = deskhub::PasscodeFromRandom(RandomU32());
    SaveUiSettings(settings);
    return settings;
}

inline std::string HostPasscode() {
    return LoadUiSettings().passcode;
}

inline std::string SessionDeviceName() {
    const std::string name = LoadUiSettings().deviceName;
    return name.empty() ? LocalDeviceName() : name;
}

}
