#include "deskhubp/system/TrustStoreFile.h"

#include "deskhubp/system/AppDataFile.h"

namespace deskhubp {

deskhub::TrustStore LoadTrustStore() {
    return deskhub::ParseTrustStore(ReadAppDataFile(kTrustStoreFileName));
}

bool SaveTrustStore(const deskhub::TrustStore& store) {
    return WriteAppDataFile(kTrustStoreFileName, deskhub::SerializeTrustStore(store));
}

deskhub::TrustVerdict CheckTrustedHost(std::string_view endpoint,
    const deskhub::Fingerprint& fingerprint) {
    return LoadTrustStore().Check(endpoint, fingerprint);
}

bool RememberTrustedHost(std::string_view endpoint, std::string_view label,
    const deskhub::Fingerprint& fingerprint, int64_t nowUnix) {
    deskhub::TrustStore store = LoadTrustStore();
    store.Remember(endpoint, label, fingerprint, nowUnix);
    return SaveTrustStore(store);
}

bool ForgetTrustedHost(std::string_view endpoint) {
    deskhub::TrustStore store = LoadTrustStore();
    if (!store.Forget(endpoint)) return false;
    return SaveTrustStore(store);
}

}
