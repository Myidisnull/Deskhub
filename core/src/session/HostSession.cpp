#include "deskhub/session/HostSession.h"

namespace deskhub {

bool HostSession::HandlePacket(std::span<const uint8_t> pkt, uint64_t nowUs) {
    const auto h = ParseCommonHeader(pkt);
    if (!h) return false;
    const auto payload = PayloadOf(pkt);

    switch (h->type) {
        case MsgType::Hello: {
            const auto m = ParseHello(payload);
            if (!m) return false;
            const State st = state();
            if ((st == State::Ready || st == State::Streaming) && m->clientId != clientId_) {
                SendReject(RejectReason::Busy);
                return false;
            }
            if (st == State::Idle) {
                if (!(m->codecMask & kCodecMaskH264)) {
                    SendReject(RejectReason::CodecMismatch);
                    return false;
                }
                clientId_ = m->clientId;
                if (!BeginSession(nowUs)) return false;
                if (cb_.onHello) cb_.onHello(*m);
                SendHelloAck(nowUs);
                return true;
            }
            lastRecvUs_ = nowUs;
            SendHelloAck(nowUs);
            return true;
        }
        case MsgType::Start:
            if (!InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            if (state() != State::Streaming) {
                state_.store(State::Streaming, std::memory_order_release);
                if (cb_.onStart) cb_.onStart();
            }
            return true;
        case MsgType::Ping: {
            if (!InSession(h->sessionId)) return false;
            const auto m = ParsePingPong(payload);
            if (!m) return false;
            lastRecvUs_ = nowUs;
            const size_t n = BuildPong(buf_, sessionId(), *m);
            if (n && cb_.send) cb_.send(std::span<const uint8_t>(buf_, n));
            return true;
        }
        case MsgType::RequestKeyframe:
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            if (cb_.onKeyframeRequest) cb_.onKeyframeRequest();
            return true;
        case MsgType::InputEvent:
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            input_.HandlePacket(payload, cb_.onInput);
            return true;
        case MsgType::SetFocus: {
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            const auto focused = ParseSetFocus(payload);
            if (focused && cb_.onFocus) cb_.onFocus(*focused);
            return true;
        }
        case MsgType::Feedback: {
            if (!InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            const auto m = ParseFeedback(payload);
            if (m && cb_.onFeedback) cb_.onFeedback(*m);
            return true;
        }
        case MsgType::Nack: {
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            uint16_t idx[kMaxNackIndices];
            uint32_t frameId = 0;
            const size_t n = ParseNack(payload, frameId, idx);
            if (n && cb_.onNack) cb_.onNack(frameId, std::span<const uint16_t>(idx, n));
            return true;
        }
        case MsgType::InvalidateRef: {
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            const auto fid = ParseInvalidateRef(payload);
            if (fid && cb_.onInvalidateRef) cb_.onInvalidateRef(*fid);
            return true;
        }
        case MsgType::Bye:
            if (!InSession(h->sessionId)) return false;
            Disconnect();
            return false;
        default:
            return false;
    }
}

void HostSession::Tick(uint64_t nowUs) {
    if (state() == State::Idle) return;
    if (nowUs - lastRecvUs_ > kSessionTimeoutUs) Disconnect();
}

bool HostSession::BeginSession(uint64_t nowUs) {
    uint32_t sid = 0;
    if (cb_.randomBytes) {
        uint8_t b[4];
        if (cb_.randomBytes(std::span<uint8_t>(b, 4)))
            sid = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
    }
    if (!sid) {
        SendReject(RejectReason::None);
        state_.store(State::Idle, std::memory_order_release);
        clientId_ = 0;
        return false;
    }
    sessionId_.store(sid, std::memory_order_relaxed);
    lastRecvUs_ = nowUs;
    state_.store(State::Ready, std::memory_order_release);
    return true;
}

void HostSession::SendHelloAck(uint64_t nowUs) {
    HelloAck a;
    a.sessionId = sessionId();
    a.codec = Codec::H264;
    a.width = offer_.width;
    a.height = offer_.height;
    a.fps = offer_.fps;
    a.bitrateBps = offer_.bitrateBps;
    a.timebaseUs = nowUs;
    const size_t n = BuildHelloAck(buf_, a);
    if (n && cb_.send) cb_.send(std::span<const uint8_t>(buf_, n));
}

void HostSession::SendReject(RejectReason reason) {
    HelloAck a{};
    a.codec = Codec::Rejected;
    a.reason = reason;
    const size_t n = BuildHelloAck(buf_, a);
    if (n && cb_.send) cb_.send(std::span<const uint8_t>(buf_, n));
}

void HostSession::Disconnect() {
    state_.store(State::Idle, std::memory_order_release);
    sessionId_.store(0, std::memory_order_relaxed);
    clientId_ = 0;
    input_.Reset();
    if (cb_.onDisconnect) cb_.onDisconnect();
}

}
