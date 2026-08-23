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

struct PairingRequest {
    uint64_t addrPacked = 0;
    std::string shortKey{};
    std::string name{};
};

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

    bool HostProvedThePasscode(const deskhub::AuthResult& result) const;

    deskhub::AuthMode Mode() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
