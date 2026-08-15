#pragma once
#include "deskhub/net/PairedDevices.h"
#include "deskhub/protocol/Wire.h"
#include "deskhubp/system/AuthProof.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deskhubp {

struct HostAuthConfig {
    HostIdentity identity{};
    // The host never keeps the digits, only what SPAKE2 needs. Callers that still hold
    // the plaintext hand it over here and it is turned into a verifier once.
    PasscodeVerifier verifier{};
    bool hasPasscode = false;
    bool allowNewPairings = true;

    void SetPasscode(const AuthSalt& salt, std::string_view passcode) {
        hasPasscode = !passcode.empty();
        verifier = hasPasscode ? MakePasscodeVerifier(salt, passcode) : PasscodeVerifier{};
    }
};

enum class HostAuthState : uint8_t {
    Idle = 0,
    AwaitingResponse = 1,
    AwaitingApproval = 2,
    Settled = 3,
};

// A machine asking to pair when no passcode is set. It waits until somebody at the
// host answers, so the request is queued for the UI thread rather than blocking the
// net loop on a dialog.
struct PairingRequest {
    uint64_t addrPacked = 0;
    std::string shortKey{};
    std::string name{};
};

// One of these per connection. It decides which of the three ways a machine may
// prove itself, checks the proof, and is the only place a passcode is ever
// consulted - the passcode itself never leaves the machine.
class HostAuth {
public:
    HostAuth();
    ~HostAuth();
    HostAuth(const HostAuth&) = delete;
    HostAuth& operator=(const HostAuth&) = delete;

    void Configure(HostAuthConfig config);

    std::optional<deskhub::AuthChallenge> Begin(const deskhub::AuthStart& start);
    deskhub::AuthResult Respond(const deskhub::AuthResponse& response, int64_t nowUnix);
    deskhub::AuthResult Approve(bool allowed, int64_t nowUnix);

    HostAuthState State() const;
    deskhub::AuthMode Mode() const;
    const deskhub::Fingerprint& PeerFingerprint() const;
    const std::string& PeerName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct ClientAuthConfig {
    HostIdentity identity{};
    std::string passcode{};
    deskhub::Fingerprint hostFingerprint{};
    std::string clientName{};
};

class ClientAuth {
public:
    ClientAuth();
    ~ClientAuth();
    ClientAuth(const ClientAuth&) = delete;
    ClientAuth& operator=(const ClientAuth&) = delete;

    void Configure(ClientAuthConfig config);

    deskhub::AuthStart Begin() const;
    std::optional<deskhub::AuthResponse> Answer(const deskhub::AuthChallenge& challenge);

    // True only when the host also proved it knows the passcode. That is what makes
    // remembering its key sound rather than a leap of faith.
    bool HostProvedThePasscode(const deskhub::AuthResult& result) const;

    deskhub::AuthMode Mode() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
