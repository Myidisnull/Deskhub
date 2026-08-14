#pragma once
#include "deskhub/diag/LogPolicy.h"
#include "deskhub/protocol/Wire.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace deskhub::ui {

enum class SessionKeyLifetime : uint8_t { PerShare = 0,
    Persistent = 1 };

struct UiSettings {
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;
    uint32_t maxDim = 1920;
    uint32_t port = kDeskhubPort;
    bool allowInput = true;
    bool clientControl = true;
    bool runInBackground = false;
    bool runInBackgroundChoiceMade = false;
    bool hideTrayIcon = false;
    uint32_t logMaxFileMb = diag::LogPolicy{}.maxFileMb;
    uint32_t logCompressAfterDays = diag::LogPolicy{}.compressAfterDays;
    uint32_t logDeleteAfterDays = diag::LogPolicy{}.deleteAfterDays;
    std::string logDir{};
    std::string passcode{};
    std::string deviceName{};
    std::string bindIp{};
    bool autostart = false;
    bool autoShare = false;
    bool clipboardSync = false;
    bool encryptSession = false;
    bool escrowSessionKey = false;
    SessionKeyLifetime sessionKeyLifetime = SessionKeyLifetime::PerShare;
    std::string sessionKeyHex{};
    std::string hostStaticSkHex{};
    std::string language{};

    bool operator==(const UiSettings&) const = default;

    diag::LogPolicy LogPolicy() const {
        return diag::ClampLogPolicy(diag::LogPolicy{
            logMaxFileMb,
            logCompressAfterDays,
            logDeleteAfterDays,
        });
    }
};

inline constexpr uint32_t kMaxSettingsFps = 240;
inline constexpr uint32_t kMaxSettingsBitrateMbps = 1000;
inline constexpr uint32_t kMaxSettingsDim = 16384;
inline constexpr uint32_t kMaxSettingsPort = 65535;

std::string TruncateDeviceName(std::string_view name);

UiSettings ParseUiSettings(std::string_view text);
std::string SerializeUiSettings(const UiSettings& settings);

}
