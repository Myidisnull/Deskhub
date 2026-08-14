#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deskhub::ui {

struct RecentDevice {
    std::string addr{};
    int64_t lastConnectedUnix = 0;
    std::string passcode{};
    bool encrypted = false;
    std::string sessionKey{};

    bool operator==(const RecentDevice&) const = default;
};

inline constexpr size_t kMaxRecentDevices = 10;

std::vector<RecentDevice> ParseRecentDevices(std::string_view text);
std::string SerializeRecentDevices(const std::vector<RecentDevice>& devices);
void TouchRecentDevice(std::vector<RecentDevice>& devices, std::string_view addr,
    int64_t nowUnix, std::string_view passcode = {}, bool encrypted = false,
    std::string_view sessionKey = {});
std::string PasscodeForDevice(const std::vector<RecentDevice>& devices, std::string_view addr);
std::string SessionKeyForDevice(const std::vector<RecentDevice>& devices, std::string_view addr);
bool EncryptedForDevice(const std::vector<RecentDevice>& devices, std::string_view addr);
void RemoveRecentDevice(std::vector<RecentDevice>& devices, std::string_view addr);

}
