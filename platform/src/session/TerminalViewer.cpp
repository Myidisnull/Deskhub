#include "deskhubp/session/TerminalViewer.h"

#include <algorithm>

#include "deskhub/ui/Strings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/TrustStoreFile.h"

namespace deskhubp {

namespace {

constexpr uint32_t kPollWaitMs = 5;
constexpr uint64_t kConnectTimeoutUs = 10'000'000;

const deskhub::term::Cell kBlankCell{};

}

const deskhub::term::Cell& TerminalSnapshot::At(uint16_t row, uint16_t col) const {
    if (row >= size.rows || col >= size.cols) return kBlankCell;
    const size_t at = size_t(row) * size.cols + col;
    if (at >= cells.size()) return kBlankCell;
    return cells[at];
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

// A scroll offset of 0 is the live screen; anything higher walks back into the
// scrollback, one row at a time, exactly as a wheel scroll does. A program on the
// alternate screen has no scrollback of its own, so there is nothing to walk into.
TerminalSnapshot TerminalViewer::Snapshot(size_t scrollOffset) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    TerminalSnapshot out;
    out.size = screen_.Size();
    out.cursor = screen_.Cursor();
    out.title = screen_.Title();
    out.revision = screen_.Revision();
    out.scrollbackRows = screen_.AlternateScreen() ? 0 : screen_.ScrollbackRows();
    out.scrollOffset = std::min(scrollOffset, out.scrollbackRows);

    const size_t history = out.scrollbackRows;
    const size_t first = history - out.scrollOffset;
    out.cells.reserve(size_t(out.size.rows) * out.size.cols);
    for (uint16_t r = 0; r < out.size.rows; ++r) {
        const size_t line = first + r;
        for (uint16_t c = 0; c < out.size.cols; ++c) {
            out.cells.push_back(line < history ? screen_.ScrollbackAt(line, c)
                                               : screen_.At(uint16_t(line - history), c));
        }
    }
    if (out.scrollOffset != 0) out.cursor.visible = false;
    return out;
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
        SetState(TerminalViewerState::Live,
            ack.resumed ? deskhub::ui::kTerminalReattached : deskhub::ui::kTerminalConnected);
    };
    hooks.onRefused = [this](deskhub::TermReason reason) {
        SetState(TerminalViewerState::Refused, deskhub::ui::TerminalRefusalText(reason));
    };
    hooks.onExit = [this](int32_t) {
        SetState(TerminalViewerState::Ended, deskhub::ui::kTerminalClosed);
    };
    client_ = std::make_unique<deskhub::TerminalClient>(std::move(hooks));

    QuicCallbacks quic;
    quic.onConnected = [this](QuicConnId id, const NetAddr&) { OnConnected(id); };
    quic.onStream = [this](QuicConnId, uint64_t, std::span<const uint8_t> bytes, bool) {
        OnStream(bytes);
    };
    quic.onClosed = [this](QuicConnId, const NetAddr&) {
        if (State() == TerminalViewerState::Live) {
            client_->LinkLost();
            SetState(TerminalViewerState::Reattaching, deskhub::ui::kTerminalReattaching);
        }
    };

    SetState(TerminalViewerState::Connecting, deskhub::ui::kTerminalConnecting);
    if (!endpoint_.Connect(QuicSettings{}, config_.host, config_.hostLabel, std::move(quic))) {
        SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
        return false;
    }

    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { Loop(); });
    return true;
}

void TerminalViewer::Stop() {
    if (!Running()) {
        endpoint_.Close();
        return;
    }
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    running_.store(false, std::memory_order_release);
    endpoint_.Close();
    client_.reset();
    const std::lock_guard<std::mutex> lock(commandMutex_);
    commands_.clear();
}

// Every call that touches the QUIC endpoint or the protocol client happens on
// this thread. The UI posts intents instead of reaching in, because a quiche
// connection is not safe to use from two threads at once.
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
    const uint64_t startedUs = NowUs();
    bool timedOut = false;
    while (!stop_.load(std::memory_order_acquire)) {
        RunCommands();
        endpoint_.Poll(NowUs(), kPollWaitMs);
        if (conn_ == 0 && !timedOut && NowUs() - startedUs > kConnectTimeoutUs) {
            timedOut = true;
            SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
        }
        if (State() == TerminalViewerState::Reattaching && endpoint_.ConnectionCount() == 0) {
            stop_.store(true, std::memory_order_release);
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

    // Same rule as the screen path: a key we have not seen before is settled by the
    // pairing handshake, but a key that CHANGED is still put to the user.
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

// The shell is only asked for after this machine has proved itself on the connection,
// exactly as the screen path does. The passcode goes into SPAKE2 and never onto the
// wire, and a machine already paired sends no passcode at all.
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

// Returns true when the message was part of proving who we are, so the terminal
// client above never sees it.
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
    if (conn_ == 0) {
        outbox_.insert(outbox_.end(), record.begin(), record.begin() + std::ptrdiff_t(written));
        return;
    }
    if (!outbox_.empty()) {
        endpoint_.SendStream(conn_, kQuicFirstTerminalStream, outbox_);
        outbox_.clear();
    }
    endpoint_.SendStream(conn_, kQuicFirstTerminalStream,
        std::span<const uint8_t>(record.data(), written));
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
