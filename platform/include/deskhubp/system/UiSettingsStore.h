#pragma once
#include "deskhub/protocol/Wire.h"
#include "deskhub/ui/UiSettings.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/system/AppDataFile.h"
#include "deskhubp/system/DeviceName.h"
#include "deskhubp/system/Random.h"

namespace deskhubp {

inline constexpr const char* kUiSettingsFileName = "ui-settings.txt";

inline void ApplyLogSettings(const deskhub::ui::UiSettings& settings) {
    SetLogPolicy(settings.LogPolicy());
    SetLogDirOverride(settings.logDir);
    ApplyLogMaintenance();
}

inline deskhub::ui::UiSettings ReadUiSettings() {
    deskhub::ui::UiSettings settings =
        deskhub::ui::ParseUiSettings(ReadAppDataFile(kUiSettingsFileName));
    if (!settings.logDir.empty() && !IsUsableLogDir(settings.logDir)) settings.logDir.clear();
    return settings;
}

inline void SaveUiSettings(const deskhub::ui::UiSettings& settings) {
    WriteAppDataFile(kUiSettingsFileName, deskhub::ui::SerializeUiSettings(settings));
    ApplyLogSettings(settings);
}

inline deskhub::ui::UiSettings LoadUiSettings() {
    deskhub::ui::UiSettings settings = ReadUiSettings();
    if (!deskhub::IsValidPasscode(settings.passcode)) {
        settings.passcode = deskhub::PasscodeFromRandom(RandomU32());
        WriteAppDataFile(kUiSettingsFileName, deskhub::ui::SerializeUiSettings(settings));
    }
    ApplyLogSettings(settings);
    return settings;
}

inline std::string HostPasscode() {
    deskhub::ui::UiSettings settings = ReadUiSettings();
    if (deskhub::IsValidPasscode(settings.passcode)) return settings.passcode;
    return LoadUiSettings().passcode;
}

inline std::string SessionDeviceName() {
    const std::string name = LoadUiSettings().deviceName;
    return name.empty() ? LocalDeviceName() : name;
}

}
