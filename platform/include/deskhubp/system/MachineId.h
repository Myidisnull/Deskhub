#pragma once

#include "deskhub/net/TrustStore.h"

namespace deskhubp {

inline constexpr const char* kMachineIdFileName = "machine_id";

deskhub::Fingerprint LoadOrCreateMachineFingerprint();

}
