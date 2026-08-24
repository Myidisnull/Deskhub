#pragma once
#include "deskhub/crypto/Keys.h"
#include "deskhub/crypto/NoiseXx.h"
#include "deskhub/crypto/TrafficCipher.h"
#include "deskhub/input/InputReceiver.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/AuthRateLimit.h"
#include "deskhub/session/HostViewers.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace deskhub {

inline constexpr uint64_t kSessionTimeoutUs = 5'000'000;
inline constexpr uint64_t kPendingAdmitTimeoutUs = 3'000'000;

inline constexpr uint32_t kMaxPasscodeAttempts = kMaxAuthFailures;
inline constexpr uint64_t kPasscodeLockoutUs = kAuthLockoutUs;

struct StreamParams {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t fps = 60;
    uint32_t bitrateBps = 20'000'000;
};

struct HostCallbacks {
    std::function<void(std::span<const uint8_t>)> send;
    std::function<void(uint64_t addrPacked, std::span<const uint8_t>)> sendTo;
    std::function<void(const Hello&)> onHello;
    std::function<void()> onStart;
    std::function<void()> onKeyframeRequest;
    std::function<void(uint64_t addrPacked, size_t viewerCount, std::string_view viewerName)>
        onViewerJoin;
    std::function<void(uint64_t addrPacked, size_t viewerCount)> onViewerLeave;
    std::function<void(uint64_t addrPacked)> onControllerChange;
    std::function<void()> onDisconnect;
    std::function<void(const Feedback&)> onFeedback;
    std::function<void(const InputEvent&)> onInput;
    std::function<void(bool focused)> onFocus;
    std::function<void(uint32_t frameId, std::span<const uint16_t> indices)> onNack;
    std::function<void(uint32_t frameId)> onInvalidateRef;
    std::function<void(std::string_view text)> onClipboardText;
    std::function<bool(std::span<uint8_t>)> randomBytes;
    std::function<void(const uint8_t key[crypto::kKeySize])> onTrafficKey;
    std::function<void()> onTrafficKeyClear;
};

class HostSession {
public:
    enum class State : uint8_t { Idle,
        Ready,
        Streaming };

    HostSession(HostCallbacks cb, StreamParams offer, ViewerBudget* hostBudget = nullptr)
        : cb_(std::move(cb)), offer_(offer) {
        viewers_.SetHostBudget(hostBudget);
    }

    void SetOffer(const StreamParams& p) {
        offer_ = p;
    }

    void SetPasscode(std::string passcode) {
        passcode_ = IsValidPasscode(passcode) ? std::move(passcode) : std::string();
    }

    void SetClipboardEnabled(bool on) {
        clipboardEnabled_ = on;
    }

    void SetEncryptRequired(bool on) {
        encryptRequired_ = on;
    }

    void SetEscrowKey(bool on) {
        escrowKey_ = on;
    }

    void SetHostStaticKey(const crypto::KeyPair& kp) {
        hostStatic_ = kp;
        haveHostStatic_ = true;
    }

    void SetSessionKey(const uint8_t key[crypto::kKeySize]) {
        std::memcpy(sessionKey_, key, crypto::kKeySize);
        haveSessionKey_ = true;
    }

    void SetTrafficCipher(crypto::TrafficCipher* cipher) {
        traffic_ = cipher;
    }

    bool HandlePacket(std::span<const uint8_t> pkt, uint64_t nowUs, uint64_t fromPacked);
    void Tick(uint64_t nowUs);
    bool KickViewer(uint64_t addrPacked);

    State state() const {
        return state_.load(std::memory_order_acquire);
    }
    uint32_t sessionId() const {
        return sessionId_.load(std::memory_order_relaxed);
    }
    size_t viewerCount() const {
        return viewers_.viewerCount();
    }
    size_t SnapshotViewerAddrs(std::span<uint64_t> out) const {
        return viewers_.SnapshotAddrs(out);
    }
    size_t SnapshotAudioViewerAddrs(std::span<uint64_t> out) const {
        return viewers_.SnapshotAudioAddrs(out);
    }
    size_t SnapshotViewerInfos(std::span<ViewerInfo> out) const {
        return viewers_.SnapshotInfos(out);
    }
    InputReceiver::Stats inputStats() const {
        return viewers_.inputStats();
    }
    uint64_t inputDenied() const {
        return inputDenied_.load(std::memory_order_relaxed);
    }
    bool encryptRequired() const {
        return encryptRequired_;
    }
    bool escrowKey() const {
        return escrowKey_;
    }

private:
    struct PendingNoise {
        uint64_t fromPacked = 0;
        uint64_t createdUs = 0;
        crypto::NoiseXx noise{};
    };

    struct PendingAdmit {
        bool used = false;
        uint64_t fromPacked = 0;
        uint64_t createdUs = 0;
        uint64_t aeadRecvCounter = 0;
        uint32_t clientId = 0;
        Hello hello{};
    };

    bool InSession(uint32_t sid) const {
        const uint32_t cur = sessionId();
        return cur != 0 && sid == cur;
    }

    bool HandleHello(std::span<const uint8_t> payload, uint64_t nowUs, uint64_t fromPacked);
    bool HandleNoise1(std::span<const uint8_t> payload, uint64_t nowUs, uint64_t fromPacked);
    bool HandleNoise3(std::span<const uint8_t> payload, uint64_t nowUs, uint64_t fromPacked);
    bool AdmitHello(const Hello& m, uint64_t nowUs, uint64_t fromPacked);
    bool QueuePendingAdmit(const Hello& m, uint64_t nowUs, uint64_t fromPacked);
    bool CompletePendingAdmit(PendingAdmit& pending, uint64_t nowUs);
    bool HandleFromViewer(const CommonHeader& header, std::span<const uint8_t> payload,
        ViewerSlot& viewer, uint64_t nowUs);
    void ApplyInput(std::span<const uint8_t> payload, ViewerSlot& viewer, uint64_t nowUs);
    void HandOverControl(uint64_t addrPacked);

    void SendHelloAck(uint64_t nowUs);
    void SendReject(RejectReason reason);
    bool BeginSession();
    bool EnsureTrafficKey();
    void DropViewer(ViewerSlot& viewer);
    void RefreshState();
    void Disconnect();
    void ExpirePending(uint64_t nowUs);
    void ClearPendingNoise(uint64_t fromPacked);
    void ClearPendingAdmit(uint64_t fromPacked);
    PendingNoise* FindPendingNoise(uint64_t fromPacked);
    PendingNoise* AllocPendingNoise(uint64_t fromPacked, uint64_t nowUs);
    PendingAdmit* FindPendingAdmit(uint64_t fromPacked);
    PendingAdmit* AllocPendingAdmit(uint64_t fromPacked, uint64_t nowUs);
    bool AllowCleartext(MsgType type) const;

    bool PasscodeAllows(const Hello& m, uint64_t nowUs, uint64_t fromPacked);
    void SendRaw(std::span<const uint8_t> pkt);
    void SendEncrypted(MsgType type, Chan chan, std::span<const uint8_t> plainPayload);

    HostCallbacks cb_;
    StreamParams offer_;
    ViewerTable viewers_;
    AuthRateLimit authLimit_;
    std::atomic<State> state_{State::Idle};
    std::atomic<uint32_t> sessionId_{0};
    std::atomic<uint64_t> inputDenied_{0};
    uint64_t controllingAddr_ = 0;
    std::string passcode_;
    bool clipboardEnabled_ = false;
    bool encryptRequired_ = false;
    bool escrowKey_ = false;
    bool haveHostStatic_ = false;
    bool haveSessionKey_ = false;
    crypto::KeyPair hostStatic_{};
    PendingNoise pendingNoise_[kMaxViewersPerHost]{};
    PendingAdmit pendingAdmit_[kMaxViewersPerHost]{};
    uint8_t sessionKey_[crypto::kKeySize]{};
    uint8_t trafficKey_[crypto::kKeySize]{};
    bool haveTrafficKey_ = false;
    crypto::TrafficCipher* traffic_ = nullptr;
    uint64_t replyTo_ = 0;
    uint8_t buf_[kMaxDatagram] = {};
    uint8_t plainBuf_[kMaxDatagram] = {};
};

}
