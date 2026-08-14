#include "deskhub/session/HostSession.h"

#include <cstring>
#include <vector>

namespace deskhub {

bool HostSession::AllowCleartext(MsgType type) const {
    return type == MsgType::Noise1 || type == MsgType::Noise3 || type == MsgType::Hello;
}

HostSession::PendingNoise* HostSession::FindPendingNoise(uint64_t fromPacked) {
    if (!fromPacked) return nullptr;
    for (PendingNoise& p : pendingNoise_)
        if (p.fromPacked == fromPacked) return &p;
    return nullptr;
}

HostSession::PendingNoise* HostSession::AllocPendingNoise(uint64_t fromPacked, uint64_t nowUs) {
    if (PendingNoise* existing = FindPendingNoise(fromPacked)) {
        existing->createdUs = nowUs;
        return existing;
    }
    for (PendingNoise& p : pendingNoise_) {
        if (p.fromPacked == 0) {
            p = PendingNoise{};
            p.fromPacked = fromPacked;
            p.createdUs = nowUs;
            return &p;
        }
    }
    size_t victim = 0;
    uint64_t oldest = pendingNoise_[0].createdUs;
    for (size_t i = 1; i < kMaxViewersPerHost; ++i) {
        if (pendingNoise_[i].createdUs < oldest) {
            oldest = pendingNoise_[i].createdUs;
            victim = i;
        }
    }
    pendingNoise_[victim] = PendingNoise{};
    pendingNoise_[victim].fromPacked = fromPacked;
    pendingNoise_[victim].createdUs = nowUs;
    return &pendingNoise_[victim];
}

void HostSession::ClearPendingNoise(uint64_t fromPacked) {
    if (PendingNoise* p = FindPendingNoise(fromPacked)) *p = PendingNoise{};
}

HostSession::PendingAdmit* HostSession::FindPendingAdmit(uint64_t fromPacked) {
    if (!fromPacked) return nullptr;
    for (PendingAdmit& p : pendingAdmit_)
        if (p.used && p.fromPacked == fromPacked) return &p;
    return nullptr;
}

HostSession::PendingAdmit* HostSession::AllocPendingAdmit(uint64_t fromPacked, uint64_t nowUs) {
    if (PendingAdmit* existing = FindPendingAdmit(fromPacked)) {
        existing->createdUs = nowUs;
        existing->aeadRecvCounter = 0;
        return existing;
    }
    for (PendingAdmit& p : pendingAdmit_) {
        if (!p.used) {
            p = PendingAdmit{};
            p.used = true;
            p.fromPacked = fromPacked;
            p.createdUs = nowUs;
            return &p;
        }
    }
    return nullptr;
}

void HostSession::ClearPendingAdmit(uint64_t fromPacked) {
    if (PendingAdmit* p = FindPendingAdmit(fromPacked)) *p = PendingAdmit{};
}

void HostSession::ExpirePending(uint64_t nowUs) {
    for (PendingNoise& p : pendingNoise_) {
        if (p.fromPacked && nowUs - p.createdUs > kPendingAdmitTimeoutUs) p = PendingNoise{};
    }
    for (PendingAdmit& p : pendingAdmit_) {
        if (p.used && nowUs - p.createdUs > kPendingAdmitTimeoutUs) p = PendingAdmit{};
    }
    if (viewers_.empty()) {
        bool anyPending = false;
        for (const PendingAdmit& p : pendingAdmit_)
            if (p.used) {
                anyPending = true;
                break;
            }
        if (!anyPending && sessionId() != 0) Disconnect();
    }
}

bool HostSession::HandlePacket(std::span<const uint8_t> pkt, uint64_t nowUs, uint64_t fromPacked) {
    const auto h0 = ParseCommonHeader(pkt);
    if (!h0) return false;

    replyTo_ = fromPacked;
    const bool wireEncrypted = (h0->flags & crypto::kHdrFlagEncrypted) != 0;

    std::span<const uint8_t> work = pkt;
    std::vector<uint8_t> decrypted;
    PendingAdmit* pendingPeer = nullptr;

    if (wireEncrypted) {
        if (!traffic_ || !traffic_->hasKey() || !fromPacked) return false;
        ViewerSlot* peer = viewers_.Find(fromPacked);
        pendingPeer = peer ? nullptr : FindPendingAdmit(fromPacked);
        if (!peer && !pendingPeer) return false;
        uint64_t& counter = peer ? peer->aeadRecvCounter : pendingPeer->aeadRecvCounter;
        auto opened = traffic_->OpenDatagram(pkt, counter);
        if (!opened) {
            if (pendingPeer) {
                authLimit_.NoteFailure(fromPacked, nowUs);
                ClearPendingAdmit(fromPacked);
                SendReject(RejectReason::WrongSessionKey);
            }
            return false;
        }
        decrypted = std::move(*opened);
        work = decrypted;
    } else if (encryptRequired_ && !AllowCleartext(h0->type)) {
        return false;
    }

    const auto h = ParseCommonHeader(work);
    if (!h) return false;
    const auto payload = PayloadOf(work);

    if (h->type == MsgType::Noise1) return HandleNoise1(payload, nowUs, fromPacked);
    if (h->type == MsgType::Noise3) return HandleNoise3(payload, nowUs, fromPacked);
    if (h->type == MsgType::Hello) return HandleHello(payload, nowUs, fromPacked);

    if (!InSession(h->sessionId)) return false;

    if (pendingPeer) {
        if (h->type != MsgType::Start && h->type != MsgType::Ping) return false;
        if (!CompletePendingAdmit(*pendingPeer, nowUs)) return false;
        pendingPeer = nullptr;
    }

    ViewerSlot* viewer = viewers_.Find(fromPacked);
    if (!viewer) return false;
    viewer->lastRecvUs = nowUs;

    if (h->type == MsgType::Bye) {
        DropViewer(*viewer);
        return false;
    }
    return HandleFromViewer(*h, payload, *viewer, nowUs);
}

bool HostSession::HandleNoise1(std::span<const uint8_t> payload, uint64_t nowUs,
    uint64_t fromPacked) {
    if (!fromPacked) return false;
    if (!encryptRequired_) {
        const size_t n = BuildNoiseDecline(buf_);
        if (n) SendRaw(std::span<const uint8_t>(buf_, n));
        return true;
    }
    if (!haveHostStatic_ || !cb_.randomBytes) {
        SendReject(RejectReason::EncryptionRequired);
        return false;
    }
    if (authLimit_.Locked(fromPacked, nowUs)) return false;

    PendingNoise* slot = AllocPendingNoise(fromPacked, nowUs);
    if (!slot) return false;
    slot->noise.Reset(crypto::NoiseRole::Responder);
    slot->noise.SetLocalStatic(hostStatic_);
    if (!slot->noise.SetLocalEphemeral(cb_.randomBytes)) return false;
    if (!slot->noise.AcceptMsg1(payload)) return false;

    if (!EnsureTrafficKey()) return false;

    uint8_t body[kMaxDatagram];
    size_t bodyN = 0;
    const std::span<const uint8_t> escrowPayload =
        escrowKey_ ? std::span<const uint8_t>(trafficKey_, crypto::kKeySize)
                   : std::span<const uint8_t>();
    if (!slot->noise.BuildMsg2(body, bodyN, escrowPayload)) return false;
    const size_t n = BuildNoise2(buf_, std::span<const uint8_t>(body, bodyN));
    if (!n) return false;
    SendRaw(std::span<const uint8_t>(buf_, n));
    return true;
}

bool HostSession::HandleNoise3(std::span<const uint8_t> payload, uint64_t nowUs,
    uint64_t fromPacked) {
    if (!encryptRequired_) return false;
    PendingNoise* slot = FindPendingNoise(fromPacked);
    if (!slot) return false;
    uint8_t helloPlain[kMaxDatagram];
    size_t helloN = 0;
    if (!slot->noise.AcceptMsg3(payload, helloPlain, helloN)) return false;
    const auto m = ParseHello(std::span<const uint8_t>(helloPlain, helloN));
    ClearPendingNoise(fromPacked);
    if (!m) return false;

    ViewerSlot* known = viewers_.Find(fromPacked);
    if (known && known->clientId != m->clientId) {
        DropViewer(*known);
        known = nullptr;
    }
    if (!known) known = viewers_.FindByClient(m->clientId);
    if (known) return AdmitHello(*m, nowUs, fromPacked);

    return QueuePendingAdmit(*m, nowUs, fromPacked);
}

bool HostSession::HandleHello(std::span<const uint8_t> payload, uint64_t nowUs,
    uint64_t fromPacked) {
    const auto m = ParseHello(payload);
    if (!m || !fromPacked) return false;
    if (encryptRequired_) {
        SendReject(RejectReason::EncryptionRequired);
        return false;
    }
    return AdmitHello(*m, nowUs, fromPacked);
}

bool HostSession::QueuePendingAdmit(const Hello& m, uint64_t nowUs, uint64_t fromPacked) {
    if (!PasscodeAllows(m, nowUs, fromPacked)) return false;
    if (!(m.codecMask & kCodecMaskH264)) {
        SendReject(RejectReason::CodecMismatch);
        return false;
    }
    if (viewers_.empty() && sessionId() == 0 && !BeginSession()) return false;
    if (!EnsureTrafficKey()) return false;

    PendingAdmit* slot = AllocPendingAdmit(fromPacked, nowUs);
    if (!slot) {
        SendReject(RejectReason::Busy);
        return false;
    }
    slot->clientId = m.clientId;
    slot->hello = m;
    if (state() == State::Idle) state_.store(State::Ready, std::memory_order_release);
    SendHelloAck(nowUs);
    return true;
}

bool HostSession::CompletePendingAdmit(PendingAdmit& pending, uint64_t nowUs) {
    const uint64_t fromPacked = pending.fromPacked;
    const uint64_t aeadRecvCounter = pending.aeadRecvCounter;
    const Hello m = pending.hello;
    pending = PendingAdmit{};

    const bool firstViewer = viewers_.empty();
    ViewerSlot* admitted = viewers_.Admit(m.clientId, fromPacked, nowUs, m.clientName);
    if (!admitted) {
        SendReject(RejectReason::Busy);
        return false;
    }
    admitted->aeadRecvCounter = aeadRecvCounter;
    RefreshState();
    if (firstViewer && cb_.onHello) cb_.onHello(m);
    if (cb_.onViewerJoin) cb_.onViewerJoin(fromPacked, viewers_.viewerCount(), m.clientName);
    return true;
}

bool HostSession::AdmitHello(const Hello& m, uint64_t nowUs, uint64_t fromPacked) {
    if (!PasscodeAllows(m, nowUs, fromPacked)) return false;
    if (!(m.codecMask & kCodecMaskH264)) {
        SendReject(RejectReason::CodecMismatch);
        return false;
    }

    ViewerSlot* known = viewers_.Find(fromPacked);
    if (known && known->clientId != m.clientId) {
        DropViewer(*known);
        known = nullptr;
    }
    if (!known) known = viewers_.FindByClient(m.clientId);

    if (known) {
        viewers_.Rebind(*known, fromPacked);
        viewers_.SetName(*known, m.clientName);
        known->lastRecvUs = nowUs;
        ClearPendingAdmit(fromPacked);
        SendHelloAck(nowUs);
        return true;
    }

    const bool firstViewer = viewers_.empty();
    if (firstViewer && sessionId() == 0 && !BeginSession()) return false;
    if (encryptRequired_ && !EnsureTrafficKey()) return false;

    if (!viewers_.Admit(m.clientId, fromPacked, nowUs, m.clientName)) {
        SendReject(RejectReason::Busy);
        return false;
    }

    ClearPendingAdmit(fromPacked);
    RefreshState();
    if (firstViewer && cb_.onHello) cb_.onHello(m);
    if (cb_.onViewerJoin) cb_.onViewerJoin(fromPacked, viewers_.viewerCount(), m.clientName);
    SendHelloAck(nowUs);
    return true;
}

bool HostSession::EnsureTrafficKey() {
    if (haveTrafficKey_) return true;
    if (!traffic_) return false;
    if (haveSessionKey_) {
        std::memcpy(trafficKey_, sessionKey_, crypto::kKeySize);
    } else {
        if (!cb_.randomBytes) return false;
        if (!cb_.randomBytes(std::span<uint8_t>(trafficKey_, crypto::kKeySize))) return false;
    }
    haveTrafficKey_ = true;
    traffic_->SetKey(trafficKey_);
    if (cb_.onTrafficKey) cb_.onTrafficKey(trafficKey_);
    return true;
}

bool HostSession::HandleFromViewer(const CommonHeader& header, std::span<const uint8_t> payload,
    ViewerSlot& viewer, uint64_t nowUs) {
    const bool streaming = state() == State::Streaming;

    switch (header.type) {
        case MsgType::Start:
            if (!viewer.started) {
                viewer.started = true;
                if (!streaming) {
                    state_.store(State::Streaming, std::memory_order_release);
                    if (cb_.onStart) cb_.onStart();
                } else if (cb_.onKeyframeRequest) {
                    cb_.onKeyframeRequest();
                }
            }
            return true;
        case MsgType::Ping: {
            const auto m = ParsePingPong(payload);
            if (!m) return false;
            const size_t n = BuildPong(buf_, sessionId(), *m);
            if (n) SendRaw(std::span<const uint8_t>(buf_, n));
            return true;
        }
        case MsgType::RequestKeyframe:
            if (!streaming) return false;
            if (cb_.onKeyframeRequest) cb_.onKeyframeRequest();
            return true;
        case MsgType::InputEvent:
            if (!streaming) return false;
            ApplyInput(payload, viewer, nowUs);
            return true;
        case MsgType::SetFocus: {
            if (!streaming) return false;
            const auto focused = ParseSetFocus(payload);
            if (!focused) return false;
            if (controllingAddr_ && controllingAddr_ != viewer.addrPacked) return true;
            if (!*focused) {
                viewer.lastInputUs = 0;
                HandOverControl(0);
            }
            if (cb_.onFocus) cb_.onFocus(*focused);
            return true;
        }
        case MsgType::Feedback: {
            const auto m = ParseFeedback(payload);
            if (!m) return false;
            viewer.feedback = *m;
            viewer.haveFeedback = true;
            if (cb_.onFeedback) cb_.onFeedback(WorstCaseFeedback(viewers_.slots()));
            return true;
        }
        case MsgType::Nack: {
            if (!streaming) return false;
            uint16_t idx[kMaxNackIndices];
            uint32_t frameId = 0;
            const size_t n = ParseNack(payload, frameId, idx);
            if (n && cb_.onNack) cb_.onNack(frameId, std::span<const uint16_t>(idx, n));
            return true;
        }
        case MsgType::InvalidateRef: {
            if (!streaming) return false;
            const auto fid = ParseInvalidateRef(payload);
            if (fid && cb_.onInvalidateRef) cb_.onInvalidateRef(*fid);
            return true;
        }
        case MsgType::Clipboard: {
            if (!streaming || !clipboardEnabled_) return false;
            const auto chunk = ParseClipboardChunk(payload);
            if (!chunk) return false;
            if (viewer.clip.Accept(*chunk)) {
                const auto text = viewer.clip.TakeCompleted();
                if (text && cb_.onClipboardText) cb_.onClipboardText(*text);
            }
            return true;
        }
        default:
            return false;
    }
}

void HostSession::ApplyInput(std::span<const uint8_t> payload, ViewerSlot& viewer,
    uint64_t nowUs) {
    if (viewers_.HigherPriorityIsDriving(viewer, nowUs)) {
        inputDenied_.fetch_add(1, std::memory_order_relaxed);
        viewer.input.HandlePacket(payload, nullptr);
        return;
    }
    viewer.lastInputUs = nowUs;
    HandOverControl(viewer.addrPacked);
    viewer.input.HandlePacket(payload, cb_.onInput);
}

void HostSession::HandOverControl(uint64_t addrPacked) {
    if (controllingAddr_ == addrPacked) return;
    controllingAddr_ = addrPacked;
    if (cb_.onControllerChange) cb_.onControllerChange(addrPacked);
}

void HostSession::Tick(uint64_t nowUs) {
    ExpirePending(nowUs);
    if (state() == State::Idle) return;
    for (ViewerSlot& s : viewers_.slots()) {
        if (!s.active) continue;
        if (nowUs - s.lastRecvUs > kSessionTimeoutUs) DropViewer(s);
    }
}

bool HostSession::KickViewer(uint64_t addrPacked) {
    ViewerSlot* viewer = viewers_.Find(addrPacked);
    if (!viewer || !viewer->active) return false;

    const size_t n = BuildBye(buf_, sessionId());
    if (n && cb_.sendTo) {
        if (traffic_ && traffic_->hasKey()) {
            const size_t e = traffic_->SealDatagram(plainBuf_, std::span<const uint8_t>(buf_, n));
            if (e) cb_.sendTo(addrPacked, std::span<const uint8_t>(plainBuf_, e));
        } else {
            cb_.sendTo(addrPacked, std::span<const uint8_t>(buf_, n));
        }
    }

    DropViewer(*viewer);
    return true;
}

void HostSession::DropViewer(ViewerSlot& viewer) {
    const uint64_t addr = viewer.addrPacked;

    viewers_.Drop(viewer);
    if (cb_.onViewerLeave) cb_.onViewerLeave(addr, viewers_.viewerCount());

    if (viewers_.empty()) {
        bool anyPending = false;
        for (const PendingAdmit& p : pendingAdmit_)
            if (p.used) {
                anyPending = true;
                break;
            }
        if (!anyPending) {
            Disconnect();
            return;
        }
        RefreshState();
        if (controllingAddr_ == addr) HandOverControl(0);
        return;
    }
    RefreshState();
    if (controllingAddr_ == addr) HandOverControl(0);
}

void HostSession::RefreshState() {
    state_.store(viewers_.anyStarted() ? State::Streaming : State::Ready,
        std::memory_order_release);
}

bool HostSession::BeginSession() {
    uint32_t sid = 0;
    if (cb_.randomBytes) {
        uint8_t b[4];
        if (cb_.randomBytes(std::span<uint8_t>(b, 4)))
            sid = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
    }
    if (!sid) {
        SendReject(RejectReason::None);
        state_.store(State::Idle, std::memory_order_release);
        return false;
    }
    sessionId_.store(sid, std::memory_order_relaxed);
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
    if (!n) return;
    if (encryptRequired_ && traffic_ && traffic_->hasKey()) {
        SendEncrypted(MsgType::HelloAck, Chan::Control, PayloadOf(std::span<const uint8_t>(buf_, n)));
    } else {
        SendRaw(std::span<const uint8_t>(buf_, n));
    }
}

bool HostSession::PasscodeAllows(const Hello& m, uint64_t nowUs, uint64_t fromPacked) {
    if (!IsValidPasscode(passcode_)) {
        SendReject(RejectReason::WrongPasscode);
        return false;
    }
    if (authLimit_.Locked(fromPacked, nowUs)) return false;

    if (m.passcode == passcode_) {
        authLimit_.NoteSuccess(fromPacked);
        return true;
    }

    authLimit_.NoteFailure(fromPacked, nowUs);
    SendReject(RejectReason::WrongPasscode);
    return false;
}

void HostSession::SendReject(RejectReason reason) {
    HelloAck a{};
    a.codec = Codec::Rejected;
    a.reason = reason;
    const size_t n = BuildHelloAck(buf_, a);
    if (n) SendRaw(std::span<const uint8_t>(buf_, n));
}

void HostSession::SendRaw(std::span<const uint8_t> pkt) {
    auto emit = [this](std::span<const uint8_t> d) {
        if (cb_.send) {
            cb_.send(d);
            return;
        }
        if (cb_.sendTo && replyTo_) cb_.sendTo(replyTo_, d);
    };
    const auto h = ParseCommonHeader(pkt);
    if (h && (h->flags & crypto::kHdrFlagEncrypted) != 0) {
        emit(pkt);
        return;
    }
    if (traffic_ && traffic_->hasKey() && h && h->sessionId != 0) {
        const size_t n = traffic_->SealDatagram(plainBuf_, pkt);
        if (n) emit(std::span<const uint8_t>(plainBuf_, n));
        return;
    }
    emit(pkt);
}

void HostSession::SendEncrypted(MsgType type, Chan chan, std::span<const uint8_t> plainPayload) {
    const size_t n = traffic_->SealInto(plainBuf_, type, chan, sessionId(), plainPayload);
    if (n) SendRaw(std::span<const uint8_t>(plainBuf_, n));
}

void HostSession::Disconnect() {
    state_.store(State::Idle, std::memory_order_release);
    sessionId_.store(0, std::memory_order_relaxed);
    viewers_.Clear();
    for (PendingNoise& p : pendingNoise_) p = PendingNoise{};
    for (PendingAdmit& p : pendingAdmit_) p = PendingAdmit{};
    HandOverControl(0);
    haveTrafficKey_ = false;
    if (traffic_) traffic_->Reset();
    crypto::SecureWipe(std::span<uint8_t>(trafficKey_, crypto::kKeySize));
    if (cb_.onTrafficKeyClear) cb_.onTrafficKeyClear();
    if (cb_.onDisconnect) cb_.onDisconnect();
}

}
