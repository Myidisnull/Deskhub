#include "deskhubp/system/PairedDevicesFile.h"

#include <cstring>

#include "deskhubp/system/AppDataFile.h"

namespace deskhubp {

AuthSalt LoadOrCreateAuthSalt() {
    const std::string stored = ReadAppDataFile(kAuthSaltFileName);
    AuthSalt salt{};
    if (stored.size() == salt.size()) {
        std::memcpy(salt.data(), stored.data(), salt.size());
        return salt;
    }
    salt = NewAuthSalt();
    WriteAppDataFile(kAuthSaltFileName,
        std::string(reinterpret_cast<const char*>(salt.data()), salt.size()));
    return salt;
}

deskhub::PairedDevices LoadPairedDevices() {
    return deskhub::ParsePairedDevices(ReadAppDataFile(kPairedDevicesFileName));
}

bool SavePairedDevices(const deskhub::PairedDevices& devices) {
    return WriteAppDataFile(kPairedDevicesFileName, deskhub::SerializePairedDevices(devices));
}

deskhub::PairVerdict CheckPairedDevice(const deskhub::Fingerprint& fingerprint) {
    return LoadPairedDevices().Check(fingerprint);
}

bool RememberPairedDevice(const deskhub::Fingerprint& fingerprint, std::string_view name,
    int64_t nowUnix) {
    deskhub::PairedDevices devices = LoadPairedDevices();
    devices.Remember(fingerprint, name, nowUnix);
    return SavePairedDevices(devices);
}

bool TouchPairedDevice(const deskhub::Fingerprint& fingerprint, std::string_view name,
    int64_t nowUnix) {
    deskhub::PairedDevices devices = LoadPairedDevices();
    if (!devices.Touch(fingerprint, name, nowUnix)) return false;
    return SavePairedDevices(devices);
}

bool ForgetPairedDevice(const deskhub::Fingerprint& fingerprint) {
    deskhub::PairedDevices devices = LoadPairedDevices();
    if (!devices.Forget(fingerprint)) return false;
    return SavePairedDevices(devices);
}

bool ForgetAllPairedDevices() {
    const bool had = LoadPairedDevices().Size() != 0;
    RemoveAppDataFile(kPairedDevicesFileName);
    return had;
}

}
