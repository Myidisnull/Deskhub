#include "deskhubp/session/AuthNegotiation.h"

#include <utility>

#include "deskhubp/diag/Log.h"
#include "deskhubp/system/PairedDevicesFile.h"

namespace deskhubp {

namespace {

constexpr std::string_view kClientLabel = "client";
constexpr std::string_view kHostLabel = "host";

}

struct HostAuth::Impl {
    HostAuthConfig config{};
    HostAuthState state = HostAuthState::Idle;
    deskhub::AuthMode mode = deskhub::AuthMode::Denied;
    deskhub::Fingerprint peer{};
    std::string peerName{};
    std::vector<uint8_t> peerPublicKey{};
    AuthNonce nonce{};
    AuthSalt salt{};
    Spake2Session spake{};
    PasscodeVerifier sharedKey{};
    bool haveSharedKey = false;

    deskhub::AuthResult Settle(deskhub::AuthResultCode code) {
        state = HostAuthState::Settled;
        deskhub::AuthResult result;
        result.code = code;
        return result;
    }

    void Pair(int64_t nowUnix) {
        RememberPairedDevice(peer, peerName, nowUnix);
    }
};

HostAuth::HostAuth() : impl_(std::make_unique<Impl>()) {
}

HostAuth::~HostAuth() = default;

void HostAuth::Configure(HostAuthConfig config) {
    impl_->config = std::move(config);
}

std::optional<deskhub::AuthChallenge> HostAuth::Begin(const deskhub::AuthStart& start) {
    const std::optional<deskhub::Fingerprint> peer = FingerprintOfPublicKey(start.publicKey);
    if (!peer) return std::nullopt;

    impl_->peer = *peer;
    impl_->peerName = start.clientName;
    impl_->peerPublicKey = start.publicKey;
    impl_->nonce = NewAuthNonce();
    impl_->haveSharedKey = false;

    deskhub::AuthChallenge challenge;
    challenge.nonce = impl_->nonce;

    const bool paired = CheckPairedDevice(*peer) == deskhub::PairVerdict::Paired;
    const bool checkCode = impl_->config.hasPasscode && start.hasPasscode;
    if (paired && !checkCode) {
        impl_->mode = deskhub::AuthMode::Signature;
    } else if (!paired && !impl_->config.allowNewPairings) {
        impl_->mode = deskhub::AuthMode::Denied;
    } else if (checkCode) {
        impl_->mode = deskhub::AuthMode::Passcode;
        impl_->salt = LoadOrCreateAuthSalt();
        challenge.salt = impl_->salt;
        if (!impl_->spake.Start(true, impl_->config.verifier, challenge.spake))
            impl_->mode = deskhub::AuthMode::Denied;
    } else {
        impl_->mode = deskhub::AuthMode::Approval;
    }

    challenge.mode = impl_->mode;
    impl_->state = impl_->mode == deskhub::AuthMode::Approval ? HostAuthState::AwaitingApproval
                                                              : HostAuthState::AwaitingResponse;
    if (impl_->mode == deskhub::AuthMode::Denied) impl_->state = HostAuthState::Settled;
    return challenge;
}

deskhub::AuthResult HostAuth::Respond(const deskhub::AuthResponse& response, int64_t nowUnix) {
    if (impl_->state != HostAuthState::AwaitingResponse)
        return impl_->Settle(deskhub::AuthResultCode::NotPaired);

    if (impl_->mode == deskhub::AuthMode::Signature) {
        const std::vector<uint8_t> transcript =
            AuthTranscript(kClientLabel, impl_->nonce, impl_->config.identity.fingerprint);
        if (!VerifySignature(impl_->peerPublicKey, transcript, response.proof))
            return impl_->Settle(deskhub::AuthResultCode::NotPaired);
        TouchPairedDevice(impl_->peer, impl_->peerName, nowUnix);
        return impl_->Settle(deskhub::AuthResultCode::Accepted);
    }

    if (impl_->mode != deskhub::AuthMode::Passcode)
        return impl_->Settle(deskhub::AuthResultCode::NotPaired);

    if (!impl_->spake.Finish(response.proof, impl_->sharedKey))
        return impl_->Settle(deskhub::AuthResultCode::WrongPasscode);
    impl_->haveSharedKey = true;

    const std::vector<uint8_t> expectedOver = AuthTranscript(kClientLabel, impl_->nonce,
        impl_->config.identity.fingerprint, impl_->peerPublicKey);
    const AuthMac expected = ComputeAuthMac(impl_->sharedKey, expectedOver);
    if (!MacsMatch(expected, response.confirm))
        return impl_->Settle(deskhub::AuthResultCode::WrongPasscode);

    impl_->Pair(nowUnix);
    deskhub::AuthResult result = impl_->Settle(deskhub::AuthResultCode::Accepted);
    result.confirm = ComputeAuthMac(impl_->sharedKey,
        AuthTranscript(kHostLabel, impl_->nonce, impl_->config.identity.fingerprint));
    return result;
}

deskhub::AuthResult HostAuth::Approve(bool allowed, int64_t nowUnix) {
    if (impl_->state != HostAuthState::AwaitingApproval)
        return impl_->Settle(deskhub::AuthResultCode::NotPaired);
    if (!allowed) return impl_->Settle(deskhub::AuthResultCode::Refused);
    impl_->Pair(nowUnix);
    return impl_->Settle(deskhub::AuthResultCode::Accepted);
}

HostAuthState HostAuth::State() const {
    return impl_->state;
}

deskhub::AuthMode HostAuth::Mode() const {
    return impl_->mode;
}

const deskhub::Fingerprint& HostAuth::PeerFingerprint() const {
    return impl_->peer;
}

const std::string& HostAuth::PeerName() const {
    return impl_->peerName;
}

struct ClientAuth::Impl {
    ClientAuthConfig config{};
    deskhub::AuthMode mode = deskhub::AuthMode::Denied;
    AuthNonce nonce{};
    Spake2Session spake{};
    PasscodeVerifier sharedKey{};
    bool haveSharedKey = false;
};

ClientAuth::ClientAuth() : impl_(std::make_unique<Impl>()) {
}

ClientAuth::~ClientAuth() = default;

void ClientAuth::Configure(ClientAuthConfig config) {
    impl_->config = std::move(config);
}

deskhub::AuthStart ClientAuth::Begin() const {
    deskhub::AuthStart start;
    start.publicKey = IdentityPublicKey(impl_->config.identity);
    start.clientName = impl_->config.clientName;
    start.hasPasscode = !impl_->config.passcode.empty();
    return start;
}

std::optional<deskhub::AuthResponse> ClientAuth::Answer(
    const deskhub::AuthChallenge& challenge) {
    impl_->mode = challenge.mode;
    impl_->nonce = challenge.nonce;
    impl_->haveSharedKey = false;

    if (challenge.mode == deskhub::AuthMode::Signature) {
        deskhub::AuthResponse response;
        response.proof = SignWithIdentity(impl_->config.identity,
            AuthTranscript(kClientLabel, challenge.nonce, impl_->config.hostFingerprint));
        if (response.proof.empty()) return std::nullopt;
        return response;
    }

    if (challenge.mode != deskhub::AuthMode::Passcode) return std::nullopt;
    if (impl_->config.passcode.empty()) return std::nullopt;

    const PasscodeVerifier verifier =
        MakePasscodeVerifier(challenge.salt, impl_->config.passcode);
    deskhub::AuthResponse response;
    if (!impl_->spake.Start(false, verifier, response.proof)) return std::nullopt;
    if (!impl_->spake.Finish(challenge.spake, impl_->sharedKey)) return std::nullopt;
    impl_->haveSharedKey = true;

    const std::vector<uint8_t> publicKey = IdentityPublicKey(impl_->config.identity);
    response.confirm = ComputeAuthMac(impl_->sharedKey,
        AuthTranscript(kClientLabel, challenge.nonce, impl_->config.hostFingerprint, publicKey));
    return response;
}

bool ClientAuth::HostProvedThePasscode(const deskhub::AuthResult& result) const {
    if (result.code != deskhub::AuthResultCode::Accepted) return false;
    if (impl_->mode != deskhub::AuthMode::Passcode || !impl_->haveSharedKey) return false;
    const AuthMac expected = ComputeAuthMac(impl_->sharedKey,
        AuthTranscript(kHostLabel, impl_->nonce, impl_->config.hostFingerprint));
    return MacsMatch(expected, result.confirm);
}

deskhub::AuthMode ClientAuth::Mode() const {
    return impl_->mode;
}

}
