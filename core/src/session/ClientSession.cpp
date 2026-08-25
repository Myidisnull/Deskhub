#include "deskhub/session/ClientSession.h"
#include "deskhub/session/LinkPulse.h"

#include "deskhub/session/HostSession.h"

#include <cstring>
#include <vector>

namespace deskhub {

void ClientSession::Start(const Hello& hello, uint64_t nowUs) {
    hello_ = hello;
    hello_.features = uint16_t(hello_.features | kFeatureEncryptCapable);
    state_ = State::Noise;
    startedUs_ = nowUs;
    lastRecvUs_ = nowUs;
    lastSentUs_ = nowUs;
    pulse_.Reset();
    lastRttUs_ = 0;
    noise3Len_ = 0;
    cipher_.Reset();
    if (havePresetKey_) cipher_.SetKey(presetKey_);
    if (!cb_.randomBytes || !crypto::GenerateKeyPair(clientStatic_, cb_.randomBytes)) {
        Die("could not create encryption keys");
        return;
    }
    SendNoise1();
}

bool ClientSession::ApplyHelloAck(const HelloAck& m, uint64_t nowUs) {
    if (m.codec == Codec::Rejected) {
        rejectReason_ = m.reason;
        switch (m.reason) {
            case RejectReason::CodecMismatch:
                Die("host rejected (codec mismatch)");
                return false;
            case RejectReason::Busy:
                Die("the host already has as many viewers as it can take");
                return false;
            case RejectReason::WrongPasscode:
                Die("wrong passcode — check the 4-digit code on the host");
                return false;
            case RejectReason::EncryptionRequired:
                Die("host requires encrypted session traffic");
                return false;
            case RejectReason::WrongSessionKey:
                Die("wrong session key — check the key on the host or paste a new one");
                return false;
            default:
                Die("host rejected (busy or codec mismatch)");
                return false;
        }
    }
    rejectReason_ = RejectReason::None;
    sessionId_ = m.sessionId;
    params_.width = m.width;
    params_.height = m.height;
    params_.fps = m.fps;
    params_.bitrateBps = m.bitrateBps;
    params_.timebaseUs = m.timebaseUs;
    state_ = State::Starting;
    noise3Len_ = 0;
    lastRecvUs_ = nowUs;
    lastSentUs_ = nowUs;
    if (cb_.onReady) cb_.onReady(params_);
    SendStart();
    return true;
}

bool ClientSession::HandlePacket(std::span<const uint8_t> pkt, uint64_t nowUs) {
    std::span<const uint8_t> work = pkt;
    std::vector<uint8_t> decrypted;
    const auto h0 = ParseCommonHeader(pkt);
    if (!h0) return false;
    const bool wireEncrypted = (h0->flags & crypto::kHdrFlagEncrypted) != 0;
    if (wireEncrypted) {
        auto opened = cipher_.OpenDatagram(pkt);
        if (!opened) {
            if (havePresetKey_ && (state_ == State::Hello || state_ == State::Noise)) {
                rejectReason_ = RejectReason::WrongSessionKey;
                Die("wrong session key — check the key on the host or paste a new one");
            }
            return false;
        }
        decrypted = std::move(*opened);
        work = decrypted;
    } else if (cipher_.hasKey()) {
        if (h0->type != MsgType::NoiseDecline && h0->type != MsgType::Noise2) return false;
    }

    const auto h = ParseCommonHeader(work);
    if (!h) return false;
    const auto payload = PayloadOf(work);

    switch (h->type) {
        case MsgType::NoiseDecline: {
            if (state_ != State::Noise) return true;
            if (havePresetKey_) {
                Die("host is not using encrypted session traffic");
                return false;
            }
            state_ = State::Hello;
            lastSentUs_ = nowUs;
            SendHello();
            return true;
        }
        case MsgType::Noise2: {
            if (state_ != State::Noise) return true;
            uint8_t traffic[crypto::kKeySize];
            size_t trafficN = 0;
            if (!noise_.AcceptMsg2(payload, std::span<uint8_t>(traffic, crypto::kKeySize), trafficN))
                return false;
            if (trafficN == crypto::kKeySize) {
                cipher_.SetKey(traffic);
                crypto::SecureWipe(std::span<uint8_t>(traffic, crypto::kKeySize));
            } else if (trafficN == 0) {
                if (!havePresetKey_ || !cipher_.hasKey()) {
                    Die("session key required — copy it from the host");
                    return false;
                }
            } else {
                return false;
            }

            const size_t helloN = BuildHello(buf_, hello_);
            if (!helloN) return false;
            uint8_t msg3[kMaxDatagram];
            size_t msg3N = 0;
            if (!noise_.BuildMsg3(std::span<uint8_t>(msg3, sizeof(msg3)), msg3N,
                    PayloadOf(std::span<const uint8_t>(buf_, helloN))))
                return false;
            const size_t n =
                BuildNoise3(buf_, std::span<const uint8_t>(msg3, msg3N), hello_.sourceId);
            if (!n) return false;
            noise3Len_ = n;
            std::memcpy(encBuf_, buf_, n);
            if (cb_.send) cb_.send(std::span<const uint8_t>(encBuf_, noise3Len_));
            state_ = State::Hello;
            lastSentUs_ = nowUs;
            lastRecvUs_ = nowUs;
            return true;
        }
        case MsgType::HelloAck: {
            const auto m = ParseHelloAck(payload);
            if (!m) return false;
            if (state_ != State::Hello && state_ != State::Noise) return true;
            return ApplyHelloAck(*m, nowUs);
        }
        case MsgType::Pong: {
            if (state_ == State::Idle || state_ == State::Dead) return false;
            if (h->sessionId != sessionId_) return false;
            const auto m = ParsePingPong(payload);
            if (!m) return false;
            lastRecvUs_ = nowUs;
            if (pulse_.OnPong(*m, nowUs)) {
                lastRttUs_ = pulse_.View(nowUs).rttUs;
                if (cb_.onRtt) cb_.onRtt(lastRttUs_);
            }
            return true;
        }
        case MsgType::Reconfig: {
            if (h->sessionId != sessionId_ || sessionId_ == 0) return false;
            if (state_ != State::Starting && state_ != State::Streaming) return false;
            const auto m = ParseReconfig(payload);
            if (!m) return false;
            lastRecvUs_ = nowUs;
            if (m->fps) params_.fps = m->fps;
            if (m->width && m->height) {
                params_.width = m->width;
                params_.height = m->height;
            }
            if (m->bitrateBps) params_.bitrateBps = m->bitrateBps;
            if (cb_.onReconfig) cb_.onReconfig(params_);
            return true;
        }
        case MsgType::Bye:
            if (h->sessionId != sessionId_ || sessionId_ == 0) return false;
            Die("host ended the session (BYE)");
            return false;
        case MsgType::Clipboard: {
            if (h->sessionId != sessionId_ || sessionId_ == 0) return false;
            if (state_ != State::Starting && state_ != State::Streaming) return false;
            const auto chunk = ParseClipboardChunk(payload);
            if (!chunk) return false;
            lastRecvUs_ = nowUs;
            if (clip_.Accept(*chunk)) {
                const auto text = clip_.TakeCompleted();
                if (text && cb_.onClipboardText) cb_.onClipboardText(*text);
            }
            return true;
        }
        default:
            return false;
    }
}

void ClientSession::NotifyVideoPacket(uint64_t nowUs) {
    if (state_ == State::Starting) state_ = State::Streaming;
    if (state_ == State::Streaming) lastRecvUs_ = nowUs;
}

void ClientSession::QueueInput(const InputEvent& e) {
    if (state_ != State::Streaming) return;
    input_.SetSessionId(sessionId_);
    input_.Queue(e);
}

void ClientSession::QueueClipboard(std::string_view text) {
    if (state_ != State::Streaming) return;
    clip_.SetSessionId(sessionId_);
    clip_.OfferLocal(text);
}

void ClientSession::SetFocused(bool on) {
    if (focusWanted_ == on && focusSent_ == on) return;
    focusWanted_ = on;
    focusRepeatsLeft_ = kFocusRepeats;
    lastFocusUs_ = 0;
}

void ClientSession::Tick(uint64_t nowUs) {
    switch (state_) {
        case State::Idle:
        case State::Dead:
            return;
        case State::Noise:
            if (nowUs - startedUs_ > kHelloGiveUpUs) {
                Die("could not connect (timed out)");
                return;
            }
            if (nowUs - lastSentUs_ >= kHelloRetryUs) {
                lastSentUs_ = nowUs;
                SendNoise1();
            }
            return;
        case State::Hello:
            if (nowUs - startedUs_ > kHelloGiveUpUs) {
                Die("could not connect (timed out)");
                return;
            }
            if (nowUs - lastSentUs_ >= kHelloRetryUs) {
                lastSentUs_ = nowUs;
                if (noise3Len_) {
                    if (cb_.send) cb_.send(std::span<const uint8_t>(encBuf_, noise3Len_));
                } else {
                    SendHello();
                }
            }
            return;
        case State::Starting:
            if (nowUs - lastSentUs_ >= kHelloRetryUs) {
                lastSentUs_ = nowUs;
                SendStart();
            }
            break;
        case State::Streaming:
            break;
    }

    if (nowUs - lastRecvUs_ > kSessionTimeoutUs) {
        Die("lost contact with host (timeout)");
        return;
    }

    if ((state_ == State::Starting || state_ == State::Streaming) && pulse_.Stalled(nowUs)) {
        Die("lost contact with host (timeout)");
        return;
    }

    if (pulse_.PingDue(nowUs) && state_ != State::Idle && state_ != State::Dead) {
        const PingPong p = pulse_.MakePing(nowUs);
        const size_t n = BuildPing(buf_, sessionId_, p);
        if (n) SendMaybeEncrypted(std::span<const uint8_t>(buf_, n));
    }

    if (state_ == State::Streaming && focusRepeatsLeft_ > 0 &&
        nowUs - lastFocusUs_ >= kFocusRetryUs) {
        lastFocusUs_ = nowUs;
        --focusRepeatsLeft_;
        focusSent_ = focusWanted_;
        const size_t n = BuildSetFocus(buf_, sessionId_, focusWanted_);
        if (n) SendMaybeEncrypted(std::span<const uint8_t>(buf_, n));
    }

    if (state_ == State::Streaming && cb_.send) {
        const auto sendFn = [this](std::span<const uint8_t> d) { SendMaybeEncrypted(d); };
        input_.Flush(nowUs, sendFn);
        clip_.Flush(nowUs, sendFn);
    }

    if (keyframeWanted_ && state_ == State::Streaming &&
        nowUs - lastKeyframeReqUs_ >= kKeyframeRetryUs) {
        lastKeyframeReqUs_ = nowUs;
        const size_t n = BuildRequestKeyframe(buf_, sessionId_);
        if (n) SendMaybeEncrypted(std::span<const uint8_t>(buf_, n));
    }
}

void ClientSession::SendFeedback(const Feedback& fb) {
    if (state_ != State::Streaming) return;
    const size_t n = BuildFeedback(buf_, sessionId_, fb);
    if (n) SendMaybeEncrypted(std::span<const uint8_t>(buf_, n));
}

void ClientSession::SendNack(uint32_t frameId, std::span<const uint16_t> indices) {
    if (state_ != State::Streaming || indices.empty()) return;
    const size_t n = BuildNack(buf_, sessionId_, frameId, indices);
    if (n) SendMaybeEncrypted(std::span<const uint8_t>(buf_, n));
}

void ClientSession::SendInvalidateRef(uint32_t frameId) {
    if (state_ != State::Streaming) return;
    const size_t n = BuildInvalidateRef(buf_, sessionId_, frameId);
    if (n) SendMaybeEncrypted(std::span<const uint8_t>(buf_, n));
}

void ClientSession::SendBye() {
    if (state_ == State::Starting || state_ == State::Streaming) {
        const size_t n = BuildBye(buf_, sessionId_);
        if (n) SendMaybeEncrypted(std::span<const uint8_t>(buf_, n));
    }
    state_ = State::Dead;
}

void ClientSession::SendNoise1() {
    uint8_t body[crypto::kNoiseEphSize];
    size_t bodyN = 0;
    noise_.Reset(crypto::NoiseRole::Initiator);
    noise_.SetLocalStatic(clientStatic_);
    if (!cb_.randomBytes || !noise_.SetLocalEphemeral(cb_.randomBytes)) {
        Die("could not create encryption keys");
        return;
    }
    if (!noise_.BuildMsg1(body, bodyN)) return;
    const size_t n =
        BuildNoise1(buf_, std::span<const uint8_t>(body, bodyN), hello_.sourceId);
    if (n && cb_.send) cb_.send(std::span<const uint8_t>(buf_, n));
}

void ClientSession::SendHello() {
    const size_t n = BuildHello(buf_, hello_);
    if (n && cb_.send) cb_.send(std::span<const uint8_t>(buf_, n));
}

void ClientSession::SendStart() {
    const size_t n = BuildStart(buf_, sessionId_);
    if (n) SendMaybeEncrypted(std::span<const uint8_t>(buf_, n));
}

void ClientSession::SendMaybeEncrypted(std::span<const uint8_t> clearPkt) {
    if (!cb_.send) return;
    if (!cipher_.hasKey()) {
        cb_.send(clearPkt);
        return;
    }
    const size_t n = cipher_.SealDatagram(encBuf_, clearPkt);
    if (n) cb_.send(std::span<const uint8_t>(encBuf_, n));
}

void ClientSession::Die(const char* reason) {
    state_ = State::Dead;
    noise3Len_ = 0;
    cipher_.Reset();
    if (cb_.onDisconnect) cb_.onDisconnect(reason);
}

}
