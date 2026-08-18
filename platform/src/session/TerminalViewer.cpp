#include "deskhubp/session/TerminalViewer.h"

#include <algorithm>

#include "deskhub/session/LinkRecovery.h"
#include "deskhub/session/TerminalSession.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/TrustStoreFile.h"

namespace deskhubp {

namespace {

constexpr uint32_t kPollWaitMs = 5;
constexpr uint64_t kConnectTimeoutUs = 10'000'000;
constexpr size_t kMaxOutboxBytes = size_t{256} * 1024;

}

TerminalViewer::~TerminalViewer() {
    Stop();
}

std::string TerminalViewer::Message() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return message_;
}

std::string TerminalViewer::Fingerprint() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return deskhub::IsZero(fingerprint_) ? std::string() : FormatFingerprint(fingerprint_);
}

void TerminalViewer::SetState(TerminalViewerState state, std::string_view message) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        message_.assign(message);
    }
    state_.store(state, std::memory_order_release);
    if (cb_.onState) cb_.onState(state, message);
}

TerminalSnapshot TerminalViewer::Snapshot(size_t scrollOffset) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return deskhub::term::SnapshotScreen(screen_, scrollOffset);
}

bool TerminalViewer::Start(const TerminalViewerConfig& config, TerminalViewerCallbacks callbacks) {
    if (Running()) return false;
    if (!QuicAvailable()) {
        SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
        return false;
    }

    config_ = config;
    cb_ = std::move(callbacks);
    conn_ = 0;
    framer_.Reset();
    outbox_.clear();
    {
        const std::lock_guard<std::mutex> lock(commandMutex_);
        commands_.clear();
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        screen_ = deskhub::term::Screen(deskhub::ClampTermSize(config_.size));
        fingerprint_ = deskhub::Fingerprint{};
        verdict_ = deskhub::TrustVerdict::Unknown;
    }

    deskhub::TerminalClientCallbacks hooks;
    hooks.send = [this](std::span<const uint8_t> message) { SendRecord(message); };
    hooks.onOutput = [this](std::span<const uint8_t> bytes) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            screen_.Write(bytes);
        }
        const std::string reply = [this] {
            const std::lock_guard<std::mutex> lock(mutex_);
            return screen_.TakeResponse();
        }();
        if (!reply.empty()) SendBytes(reply);
        if (cb_.onRedraw) cb_.onRedraw();
    };
    hooks.onOpened = [this](const deskhub::TermOpenAck& ack) {
        RememberIfPasscodeProvedIt();
        redialAttempts_ = 0;
        linkLostAtUs_ = 0;
        lastKeepaliveUs_ = NowUs();
        SetState(TerminalViewerState::Live,
            ack.resumed ? deskhub::ui::kTerminalReattached : deskhub::ui::kTerminalConnected);
    };
    hooks.onRefused = [this](deskhub::TermReason reason) {
        if (Recovering() && client_->CanReattach()) {
            SetState(TerminalViewerState::Reattaching, deskhub::ui::kTerminalReattaching);
            return;
        }
        SetState(TerminalViewerState::Refused, deskhub::ui::TerminalRefusalText(reason));
    };
    hooks.onExit = [this](int32_t) {
        SetState(TerminalViewerState::Ended, deskhub::ui::kTerminalClosed);
    };
    client_ = std::make_unique<deskhub::TerminalClient>(std::move(hooks));

    keepaliveIntervalUs_ = deskhub::KeepaliveIntervalUs(QuicSettings{}.idleTimeoutMs);
    redialAttempts_ = 0;
    linkLostAtUs_ = 0;

    if (!Dial()) {
        SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
        return false;
    }

    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { Loop(); });
    return true;
}

bool TerminalViewer::Dial() {
    conn_ = 0;
    framer_.Reset();
    dialStartedUs_ = NowUs();
    dialTimedOut_ = false;
    lastKeepaliveUs_ = dialStartedUs_;

    QuicCallbacks quic;
    quic.onConnected = [this](QuicConnId id, const NetAddr&) { OnConnected(id); };
    quic.onStream = [this](QuicConnId, uint64_t, std::span<const uint8_t> bytes, bool) {
        OnStream(bytes);
    };
    quic.onClosed = [this](QuicConnId, const NetAddr&) { OnLinkLost(); };

    SetState(TerminalViewerState::Connecting, deskhub::ui::kTerminalConnecting);
    return endpoint_.Connect(QuicSettings{}, config_.host, config_.hostLabel, std::move(quic));
}

void TerminalViewer::OnLinkLost() {
    conn_ = 0;
    if (State() != TerminalViewerState::Live) return;
    client_->LinkLost();
    linkLostAtUs_ = NowUs();
    redialAttempts_ = 0;
    redialAtUs_ = linkLostAtUs_ + deskhub::ReconnectDelayUs(0);
    LOGW("terminal: link to %s dropped \xE2\x80\x94 reattaching",
        config_.host.ToString().c_str());
    SetState(TerminalViewerState::Reattaching, deskhub::ui::kTerminalReattaching);
}

bool TerminalViewer::Recovering() const {
    if (linkLostAtUs_ == 0) return false;
    switch (State()) {
        case TerminalViewerState::Reattaching:
        case TerminalViewerState::Connecting:
        case TerminalViewerState::Opening: return true;
        default: return false;
    }
}

void TerminalViewer::KeepLinkAlive(uint64_t nowUs) {
    if (conn_ == 0 || State() != TerminalViewerState::Live) return;
    if (!deskhub::KeepaliveDue(nowUs, lastKeepaliveUs_, keepaliveIntervalUs_)) return;
    endpoint_.SendKeepalive(conn_);
    lastKeepaliveUs_ = nowUs;
}

void TerminalViewer::RecoverLink(uint64_t nowUs) {
    if (!deskhub::ReconnectStillWorthTrying(nowUs - linkLostAtUs_,
            deskhub::kTerminalReattachGraceUs)) {
        LOGW("terminal: gave up reattaching to %s after %u attempts",
            config_.host.ToString().c_str(), redialAttempts_);
        SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
        stop_.store(true, std::memory_order_release);
        return;
    }
    if (nowUs < redialAtUs_) return;

    const bool linkUp = conn_ != 0;
    const bool dialRanOut = nowUs - dialStartedUs_ > kConnectTimeoutUs;
    if (!linkUp && endpoint_.ConnectionCount() != 0 && !dialRanOut) return;

    ++redialAttempts_;
    redialAtUs_ = nowUs + deskhub::ReconnectDelayUs(redialAttempts_);

    if (linkUp && client_->CanReattach()) {
        client_->Reattach();
        return;
    }

    endpoint_.Close();
    if (!Dial()) SetState(TerminalViewerState::Reattaching, deskhub::ui::kTerminalReattaching);
}

void TerminalViewer::Stop() {
    if (!Running()) {
        endpoint_.Close();
        return;
    }
    if (State() == TerminalViewerState::Live) {
        Post([this] { client_->Close(); });
    }
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    running_.store(false, std::memory_order_release);
    endpoint_.Close();
    client_.reset();
    const std::lock_guard<std::mutex> lock(commandMutex_);
    commands_.clear();
}

void TerminalViewer::Post(std::function<void()> command) {
    const std::lock_guard<std::mutex> lock(commandMutex_);
    commands_.push_back(std::move(command));
}

void TerminalViewer::RunCommands() {
    std::vector<std::function<void()>> todo;
    {
        const std::lock_guard<std::mutex> lock(commandMutex_);
        todo.swap(commands_);
    }
    for (const std::function<void()>& command : todo) command();
}

void TerminalViewer::Loop() {
    while (!stop_.load(std::memory_order_acquire)) {
        RunCommands();
        endpoint_.Poll(NowUs(), kPollWaitMs);

        FlushOutbox();

        const uint64_t nowUs = NowUs();
        KeepLinkAlive(nowUs);

        if (Recovering()) {
            RecoverLink(nowUs);
            continue;
        }
        if (conn_ == 0 && !dialTimedOut_ && nowUs - dialStartedUs_ > kConnectTimeoutUs) {
            dialTimedOut_ = true;
            SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
        }
    }
    RunCommands();
}

void TerminalViewer::OnConnected(QuicConnId conn) {
    conn_ = conn;
    const std::optional<deskhub::Fingerprint> peer = endpoint_.PeerFingerprint(conn);
    if (!peer) {
        SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
        return;
    }

    const std::string endpointName = config_.host.ToString();
    const deskhub::TrustVerdict verdict = CheckTrustedHost(endpointName, *peer);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        fingerprint_ = *peer;
        verdict_ = verdict;
    }

    if (verdict == deskhub::TrustVerdict::Changed) {
        SetState(TerminalViewerState::Deciding, deskhub::ui::kTrustChangedBody);
        if (cb_.onTrustAsked) cb_.onTrustAsked(verdict, FormatFingerprint(*peer));
        return;
    }
    autoTrustPending_.store(verdict == deskhub::TrustVerdict::Unknown,
        std::memory_order_release);

    SetState(TerminalViewerState::Opening, deskhub::ui::kTerminalConnecting);
    BeginAuth();
}

void TerminalViewer::BeginAuth() {
    ClientAuthConfig config;
    config.identity = LoadOrCreateHostIdentity(config_.clientName);
    config.passcode = config_.passcode;
    config.hostFingerprint = fingerprint_;
    config.clientName = config_.clientName;
    auth_ = std::make_unique<ClientAuth>();
    auth_->Configure(std::move(config));

    std::vector<uint8_t> out(deskhub::kMaxRecordSize);
    out.resize(deskhub::BuildAuthStart(out, auth_->Begin()));
    SendRecord(out);
}

bool TerminalViewer::HandleAuth(std::span<const uint8_t> message) {
    if (!auth_) return false;
    const std::optional<deskhub::CommonHeader> header = deskhub::ParseCommonHeader(message);
    if (!header) return false;
    const std::span<const uint8_t> payload = deskhub::PayloadOf(message);

    if (header->type == deskhub::MsgType::AuthChallenge) {
        const std::optional<deskhub::AuthChallenge> challenge =
            deskhub::ParseAuthChallenge(payload);
        if (!challenge) return true;
        if (challenge->mode == deskhub::AuthMode::Approval) return true;

        const std::optional<deskhub::AuthResponse> response = auth_->Answer(*challenge);
        if (!response) {
            SetState(TerminalViewerState::Refused,
                deskhub::ui::AuthRefusalText(challenge->mode == deskhub::AuthMode::Denied
                                                 ? deskhub::AuthResultCode::PairingDisabled
                                                 : deskhub::AuthResultCode::WrongPasscode));
            return true;
        }
        std::vector<uint8_t> out(deskhub::kMaxRecordSize);
        out.resize(deskhub::BuildAuthResponse(out, *response));
        SendRecord(out);
        return true;
    }

    if (header->type != deskhub::MsgType::AuthResult) return false;
    const std::optional<deskhub::AuthResult> result = deskhub::ParseAuthResult(payload);
    if (!result) return true;
    if (result->code != deskhub::AuthResultCode::Accepted) {
        SetState(TerminalViewerState::Refused, deskhub::ui::AuthRefusalText(result->code));
        return true;
    }
    if (auth_->HostProvedThePasscode(*result)) RememberIfPasscodeProvedIt();
    if (client_->CanReattach()) {
        client_->Reattach();
        return true;
    }
    client_->Open(std::string(), config_.size, config_.clientName);
    return true;
}

void TerminalViewer::RememberIfPasscodeProvedIt() {
    if (!autoTrustPending_.exchange(false, std::memory_order_acq_rel)) return;
    deskhub::Fingerprint peer;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        peer = fingerprint_;
        verdict_ = deskhub::TrustVerdict::Trusted;
    }
    if (deskhub::IsZero(peer)) return;
    RememberTrustedHost(config_.host.ToString(), config_.hostLabel, peer, NowUnixSeconds());
    LOGI("terminal: passcode accepted \xE2\x80\x94 remembering %s as %s",
        config_.host.ToString().c_str(), FormatFingerprint(peer).c_str());
}

void TerminalViewer::AcceptFingerprint() {
    if (State() != TerminalViewerState::Deciding) return;
    deskhub::Fingerprint peer;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        peer = fingerprint_;
    }
    RememberTrustedHost(config_.host.ToString(), config_.hostLabel, peer, NowUnixSeconds());
    SetState(TerminalViewerState::Opening, deskhub::ui::kTerminalConnecting);
    Post([this] { BeginAuth(); });
}

void TerminalViewer::RejectFingerprint() {
    SetState(TerminalViewerState::Ended, deskhub::ui::kTrustReject);
    stop_.store(true, std::memory_order_release);
}

void TerminalViewer::OnStream(std::span<const uint8_t> bytes) {
    framer_.Append(bytes);
    std::vector<uint8_t> message;
    while (framer_.Next(message)) {
        if (HandleAuth(message)) continue;
        client_->HandleMessage(message);
    }
    if (framer_.Failed()) SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
}

void TerminalViewer::SendRecord(std::span<const uint8_t> message) {
    std::vector<uint8_t> record(deskhub::kRecordPrefixSize + message.size());
    const size_t written = deskhub::BuildRecord(record, message);
    if (written == 0) return;
    if (outbox_.size() + written > kMaxOutboxBytes) return;
    outbox_.insert(outbox_.end(), record.begin(), record.begin() + std::ptrdiff_t(written));
    FlushOutbox();
}

void TerminalViewer::FlushOutbox() {
    if (conn_ == 0 || outbox_.empty()) return;
    if (endpoint_.SendStream(conn_, kQuicControlStream, outbox_)) outbox_.clear();
}

void TerminalViewer::SendBytes(const std::string& bytes) {
    if (bytes.empty() || !Running()) return;
    Post([this, bytes] {
        client_->SendInput(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
    });
}

deskhub::term::TerminalModes TerminalViewer::CurrentModes() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return screen_.Modes();
}

void TerminalViewer::SendKey(const deskhub::term::TermKeyEvent& key) {
    SendBytes(deskhub::term::EncodeKey(key, CurrentModes()));
}

void TerminalViewer::SendText(std::string_view text) {
    SendBytes(deskhub::term::EncodeText(text, CurrentModes()));
}

void TerminalViewer::Paste(std::string_view text) {
    SendBytes(deskhub::term::EncodePaste(text, CurrentModes()));
}

void TerminalViewer::Resize(deskhub::TermSize size) {
    const deskhub::TermSize clamped = deskhub::ClampTermSize(size);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (clamped == screen_.Size()) return;
        screen_.Resize(clamped);
    }
    if (!Running()) {
        config_.size = clamped;
        return;
    }
    Post([this, clamped] {
        config_.size = clamped;
        client_->Resize(clamped);
    });
    if (cb_.onRedraw) cb_.onRedraw();
}

}
