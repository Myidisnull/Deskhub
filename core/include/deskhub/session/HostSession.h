#pragma once
#include "deskhub/input/InputReceiver.h"
#include "deskhub/protocol/Wire.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace deskhub {

inline constexpr uint64_t kSessionTimeoutUs = 5'000'000;

struct StreamParams {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t fps = 60;
    uint32_t bitrateBps = 20'000'000;
};

struct HostCallbacks {
    std::function<void(std::span<const uint8_t>)> send;
    std::function<void(const Hello&)> onHello;
    std::function<void()> onStart;
    std::function<void()> onKeyframeRequest;
    std::function<void()> onDisconnect;
    std::function<void(const Feedback&)> onFeedback;
    std::function<void(const InputEvent&)> onInput;
    std::function<void(bool focused)> onFocus;
    std::function<void(uint32_t frameId, std::span<const uint16_t> indices)> onNack;
    std::function<void(uint32_t frameId)> onInvalidateRef;
    std::function<bool(std::span<uint8_t>)> randomBytes;
};

class HostSession {
public:
    enum class State : uint8_t { Idle,
        Ready,
        Streaming };

    HostSession(HostCallbacks cb, StreamParams offer)
        : cb_(std::move(cb)), offer_(offer) {}

    void SetOffer(const StreamParams& p) {
        offer_ = p;
    }

    bool HandlePacket(std::span<const uint8_t> pkt, uint64_t nowUs);
    void Tick(uint64_t nowUs);

    State state() const {
        return state_.load(std::memory_order_acquire);
    }
    uint32_t sessionId() const {
        return sessionId_.load(std::memory_order_relaxed);
    }
    const InputReceiver::Stats& inputStats() const {
        return input_.stats();
    }

private:
    bool InSession(uint32_t sid) const {
        const uint32_t cur = sessionId();
        return cur != 0 && sid == cur;
    }

    void SendHelloAck(uint64_t nowUs);
    void SendReject(RejectReason reason);
    bool BeginSession(uint64_t nowUs);
    void Disconnect();

    HostCallbacks cb_;
    StreamParams offer_;
    InputReceiver input_;
    std::atomic<State> state_{State::Idle};
    std::atomic<uint32_t> sessionId_{0};
    uint32_t clientId_ = 0;
    uint64_t lastRecvUs_ = 0;
    uint8_t buf_[kMaxDatagram] = {};
};

}
