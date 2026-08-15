#pragma once
#include "deskhub/net/PairedDevices.h"
#include "deskhubp/system/AuthProof.h"

namespace deskhubp {

inline constexpr const char* kPairedDevicesFileName = "paired_devices";
inline constexpr const char* kAuthSaltFileName = "auth_salt";

// Fixed for the life of the machine, because the verifier the host keeps is derived
// from it. It is not a secret - it goes to every machine that asks - it just stops
// one stored verifier from being the same as another machine's.
AuthSalt LoadOrCreateAuthSalt();

deskhub::PairedDevices LoadPairedDevices();
bool SavePairedDevices(const deskhub::PairedDevices& devices);

deskhub::PairVerdict CheckPairedDevice(const deskhub::Fingerprint& fingerprint);
bool RememberPairedDevice(const deskhub::Fingerprint& fingerprint, std::string_view name,
    int64_t nowUnix);
bool TouchPairedDevice(const deskhub::Fingerprint& fingerprint, std::string_view name,
    int64_t nowUnix);
bool ForgetPairedDevice(const deskhub::Fingerprint& fingerprint);
bool ForgetAllPairedDevices();

}
