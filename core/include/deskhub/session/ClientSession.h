#pragma once
#include "deskhub/crypto/Keys.h"
#include "deskhub/crypto/NoiseXx.h"
#include "deskhub/crypto/TrafficCipher.h"
#include "deskhub/input/InputSender.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/ClipboardSync.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace deskhub {

inline constexpr uint64_t kHelloRetryUs = 500'000;
inline constexpr uint64_t kHelloGiveUpUs = 10'000'000;
inline constexpr uint64_t kPingIntervalUs = 1'000'000;
inline constexpr uint64_t kKeyframeRetryUs = 250'000;
inline constexpr uint64_t kFocusRetryUs = 50'000;
inline constexpr int kFocusRepeats = 3;

struct NegotiatedParams {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t fps = 60;
    uint32_t bitrateBps = 0;
    uint64_t timebaseUs = 0;
};

struct ClientCallbacks {
    std::function<void(std::span<const uint8_t>)> send;
    std::function<void(const NegotiatedParams&)> onReady;
    std::function<void(const NegotiatedParams&)> onReconfig;
    std::function<void(uint32_t rttUs)> onRtt;
    std::function<void(const char* reason)> onDisconnect;
    std::function<void(std::string_view text)> onClipboardText;
    std::function<bool(std::span<uint8_t>)> randomBytes;
};

class ClientSession {
public:
    enum class State : uint8_t { Idle,
        Noise,
        Hello,
        Starting,
        Streaming,
        Dead };

    explicit ClientSession(ClientCallbacks cb)
        : cb_(std::move(cb)) {}

    void SetSessionKey(const uint8_t key[crypto::kKeySize]) {
        std::memcpy(presetKey_, key, crypto::kKeySize);
        havePresetKey_ = true;
    }

    void ClearSessionKey() {
        havePresetKey_ = false;
        crypto::SecureWipe(std::span<uint8_t>(presetKey_, crypto::kKeySize));
    }

    void Start(const Hello& hello, uint64_t nowUs);

    RejectReason rejectReason() const {
        return rejectReason_;
    }

    bool HandlePacket(std::span<const uint8_t> pkt, uint64_t nowUs);

    void NotifyVideoPacket(uint64_t nowUs);

    void Tick(uint64_t nowUs);

    void QueueInput(const InputEvent& e);

    void QueueClipboard(std::string_view text);

    void SetFocused(bool on);

    void RequestKeyframe() {
        keyframeWanted_ = true;
    }
    void CancelKeyframeRequest() {
        keyframeWanted_ = false;
    }

    void SendFeedback(const Feedback& fb);

    void SendNack(uint32_t frameId, std::span<const uint16_t> indices);

    void SendInvalidateRef(uint32_t frameId);

    void SendBye();

    State state() const {
        return state_;
    }
    uint32_t sessionId() const {
        return sessionId_;
    }
    uint32_t lastRttUs() const {
        return lastRttUs_;
    }
    const NegotiatedParams& params() const {
        return params_;
    }
    crypto::TrafficCipher& cipher() {
        return cipher_;
    }
    bool encrypted() const {
        return cipher_.hasKey();
    }

private:
    void SendNoise1();
    void SendHello();
    void SendStart();
    void Die(const char* reason);
    bool ApplyHelloAck(const HelloAck& m, uint64_t nowUs);
    void SendMaybeEncrypted(std::span<const uint8_t> clearPkt);

    ClientCallbacks cb_;
    InputSender input_;
    ClipboardSync clip_;
    State state_ = State::Idle;
    uint32_t sessionId_ = 0;
    Hello hello_{};
    NegotiatedParams params_{};
    uint64_t startedUs_ = 0;
    uint64_t lastSentUs_ = 0;
    uint64_t lastRecvUs_ = 0;
    uint64_t lastPingUs_ = 0;
    uint64_t lastKeyframeReqUs_ = 0;
    uint64_t lastFocusUs_ = 0;
    int focusRepeatsLeft_ = 0;
    bool focusWanted_ = false;
    bool focusSent_ = false;
    uint32_t nextPingId_ = 1;
    uint32_t lastRttUs_ = 0;
    bool keyframeWanted_ = false;
    RejectReason rejectReason_ = RejectReason::None;
    crypto::NoiseXx noise_{};
    crypto::KeyPair clientStatic_{};
    crypto::TrafficCipher cipher_{};
    uint8_t presetKey_[crypto::kKeySize]{};
    bool havePresetKey_ = false;
    uint8_t buf_[kMaxDatagram] = {};
    uint8_t encBuf_[kMaxDatagram] = {};
    size_t noise3Len_ = 0;
};

}
