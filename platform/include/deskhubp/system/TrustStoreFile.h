#pragma once
#include "deskhub/net/TrustStore.h"

namespace deskhubp {

inline constexpr const char* kTrustStoreFileName = "known_hosts";

deskhub::TrustStore LoadTrustStore();
bool SaveTrustStore(const deskhub::TrustStore& store);

deskhub::TrustVerdict CheckTrustedHost(std::string_view endpoint,
    const deskhub::Fingerprint& fingerprint);
bool RememberTrustedHost(std::string_view endpoint, std::string_view label,
    const deskhub::Fingerprint& fingerprint, int64_t nowUnix);
bool ForgetTrustedHost(std::string_view endpoint);

}
