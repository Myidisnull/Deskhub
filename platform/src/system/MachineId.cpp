#include "deskhubp/system/MachineId.h"

#include "deskhubp/system/AppDataFile.h"
#include "deskhubp/system/Random.h"

namespace deskhubp {

deskhub::Fingerprint LoadOrCreateMachineFingerprint() {
    const std::string stored = ReadAppDataFile(kMachineIdFileName);
    if (const auto parsed = deskhub::ParseFingerprint(stored)) return *parsed;

    deskhub::Fingerprint fp{};
    RandomBytes(fp.bytes.data(), fp.bytes.size());
    WriteAppDataFile(kMachineIdFileName, deskhub::FormatFingerprint(fp));
    return fp;
}

}
