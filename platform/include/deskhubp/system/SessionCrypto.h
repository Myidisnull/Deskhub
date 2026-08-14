#pragma once
#include "deskhub/crypto/KeyCodec.h"
#include "deskhub/media/AgentTypes.h"
#include "deskhub/ui/UiSettings.h"
#include "deskhubp/system/Random.h"
#include "deskhubp/system/UiSettingsStore.h"

#include <cstring>
#include <span>

namespace deskhubp {

inline bool ApplyEncryptToAgentOptions(deskhub::ui::UiSettings& settings,
    deskhub::media::AgentOptions& opt) {
    opt.encryptSession = settings.encryptSession;
    opt.escrowSessionKey = false;
    opt.sessionKey.clear();
    opt.hostStaticSk.clear();
    if (!settings.encryptSession) {
        settings.escrowSessionKey = false;
        return true;
    }

    opt.escrowSessionKey = settings.escrowSessionKey;
    auto random = [](std::span<uint8_t> out) {
        return RandomBytes(out.data(), out.size());
    };

    deskhub::crypto::KeyPair kp{};
    if (!deskhub::crypto::LoadOrCreateHostStaticKey(settings.hostStaticSkHex, kp, random))
        return false;
    opt.hostStaticSk.assign(kp.sk, kp.sk + deskhub::crypto::kKeySize);

    uint8_t session[deskhub::crypto::kKeySize];
    const bool refresh =
        settings.sessionKeyLifetime == deskhub::ui::SessionKeyLifetime::PerShare ||
        settings.sessionKeyHex.size() != 64;
    const bool ok = refresh
                        ? deskhub::crypto::RefreshSessionKey(settings.sessionKeyHex, session, random)
                        : deskhub::crypto::LoadOrCreateSessionKey(settings.sessionKeyHex, session,
                              random);
    if (!ok) return false;
    opt.sessionKey.assign(session, session + deskhub::crypto::kKeySize);
    deskhub::crypto::SecureWipe(std::span<uint8_t>(session, sizeof(session)));
    SaveUiSettings(settings);
    return true;
}

inline bool EnsureSessionKeyMaterial(deskhub::ui::UiSettings& settings, bool forceRefresh) {
    if (!settings.encryptSession) return true;
    auto random = [](std::span<uint8_t> out) {
        return RandomBytes(out.data(), out.size());
    };
    deskhub::crypto::KeyPair kp{};
    if (!deskhub::crypto::LoadOrCreateHostStaticKey(settings.hostStaticSkHex, kp, random))
        return false;
    uint8_t session[deskhub::crypto::kKeySize];
    const bool ok =
        forceRefresh || settings.sessionKeyHex.size() != 64
            ? deskhub::crypto::RefreshSessionKey(settings.sessionKeyHex, session, random)
            : deskhub::crypto::LoadOrCreateSessionKey(settings.sessionKeyHex, session, random);
    deskhub::crypto::SecureWipe(std::span<uint8_t>(session, sizeof(session)));
    if (!ok) return false;
    SaveUiSettings(settings);
    return true;
}

}
