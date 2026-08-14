#pragma once

#include "deskhub/ui/Brand.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace deskhub::diag {

struct LogPolicy {
    uint32_t maxFileMb = 10;
    uint32_t compressAfterDays = 7;
    uint32_t deleteAfterDays = 30;

    bool operator==(const LogPolicy&) const = default;
};

inline constexpr uint32_t kMinLogMaxFileMb = 1;
inline constexpr uint32_t kMaxLogMaxFileMb = 1024;
inline constexpr uint32_t kMaxLogRetentionDays = 3650;
inline constexpr size_t kMaxLogDirChars = 1024;

inline bool IsPlausibleLogDir(std::string_view path) {
    if (path.empty()) return true;
    if (path.size() > kMaxLogDirChars) return false;
    for (char c : path) {
        if (c == '\0' || c == '\n' || c == '\r') return false;
    }
    return true;
}

inline LogPolicy ClampLogPolicy(LogPolicy policy) {
    if (policy.maxFileMb < kMinLogMaxFileMb) policy.maxFileMb = kMinLogMaxFileMb;
    if (policy.maxFileMb > kMaxLogMaxFileMb) policy.maxFileMb = kMaxLogMaxFileMb;
    if (policy.compressAfterDays > kMaxLogRetentionDays)
        policy.compressAfterDays = kMaxLogRetentionDays;
    if (policy.deleteAfterDays > kMaxLogRetentionDays)
        policy.deleteAfterDays = kMaxLogRetentionDays;
    if (policy.deleteAfterDays > 0 && policy.compressAfterDays > 0 &&
        policy.deleteAfterDays < policy.compressAfterDays) {
        policy.deleteAfterDays = policy.compressAfterDays;
    }
    return policy;
}

inline uint64_t LogMaxFileBytes(const LogPolicy& policy) {
    return uint64_t(ClampLogPolicy(policy).maxFileMb) * 1024ull * 1024ull;
}

inline std::string LogNamePrefix() {
    return std::string(brand::kLogFilePrefix) + "-";
}

inline bool IsDeskhubLogName(std::string_view name) {
    const std::string prefix = LogNamePrefix();
    constexpr std::string_view kSuffix = ".log";
    if (name.size() < prefix.size() + kSuffix.size()) return false;
    if (name.compare(0, prefix.size(), prefix) != 0) return false;
    if (name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) return false;
    if (name.size() >= 7 && name.compare(name.size() - 7, 7, ".log.gz") == 0) return false;
    return true;
}

inline bool IsDeskhubGzipLogName(std::string_view name) {
    const std::string prefix = LogNamePrefix();
    constexpr std::string_view kSuffix = ".log.gz";
    if (name.size() < prefix.size() + kSuffix.size()) return false;
    if (name.compare(0, prefix.size(), prefix) != 0) return false;
    return name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
}

}
