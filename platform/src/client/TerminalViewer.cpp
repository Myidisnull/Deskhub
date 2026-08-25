#include "deskhubp/client/TerminalViewer.h"

#include "deskhub/session/ClientReconnect.h"
#include "deskhub/session/TerminalSession.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/MachineId.h"

#include <utility>

namespace deskhubp {
namespace {

constexpr uint32_t kRecvWaitMs = 20;
constexpr size_t kMaxOutboxBytes = size_t{256} * 1024;
constexpr uint64_t kRecvSilentUs = 8'000'000;
constexpr uint64_t kPairingLegacyUs = 400'000;
constexpr uint64_t kPairingWaitUs = 8'000'000;
constexpr uint64_t kPairingRetryUs = 1'000'000;

}

TerminalViewer::~TerminalViewer() {
    try {
        Stop();
    } catch (...) {
        outbox_.clear();
    }
}

std::string TerminalViewer::Message() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return message_;
}

std::string TerminalViewer::Fingerprint() const {
    return {};
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

    config_ = config;
    cb_ = std::move(callbacks);
    outbox_.clear();
    outboxBytes_ = 0;
    lostAtUs_ = 0;
    resumeRetryAtUs_ = 0;
    resumeAttempts_ = 0;
    lastRecvUs_ = 0;
    stream_ = deskhub::RecordStream{};
    {
        const std::lock_guard<std::mutex> lock(commandMutex_);
        commands_.clear();
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        screen_ = deskhub::term::Screen(deskhub::ClampTermSize(config_.size));
    }

    if (!sock_.Open(0)) {
        SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
        return false;
    }
    sock_.SetRecvTimeout(kRecvWaitMs);

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
        if (cb_.onOutput) cb_.onOutput(bytes);
        if (cb_.onRedraw) cb_.onRedraw();
    };
    hooks.onOpened = [this](const deskhub::TermOpenAck& ack) {
        lostAtUs_ = 0;
        resumeRetryAtUs_ = 0;
        resumeAttempts_ = 0;
        lastRecvUs_ = NowUs();
        SetState(TerminalViewerState::Live,
            ack.resumed ? deskhub::ui::kTerminalReattached : deskhub::ui::kTerminalConnected);
    };
    hooks.onRefused = [this](deskhub::TermReason reason) {
        if (lostAtUs_ != 0 && client_ && client_->CanReattach()) {
            SetState(TerminalViewerState::Reattaching, deskhub::ui::kTerminalReattaching);
            resumeRetryAtUs_ = NowUs() + deskhub::ClientReconnectBackoffUs(int(resumeAttempts_));
            ++resumeAttempts_;
            return;
        }
        SetState(TerminalViewerState::Refused, deskhub::ui::TerminalRefusalText(reason));
    };
    hooks.onExit = [this](int32_t) {
        SetState(TerminalViewerState::Ended, deskhub::ui::kTerminalClosed);
    };
    client_ = std::make_unique<deskhub::TerminalClient>(std::move(hooks));

    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    SetState(TerminalViewerState::Opening, deskhub::ui::kTerminalConnecting);
    BeginOpen();
    thread_ = std::thread([this] { Loop(); });
    return true;
}

void TerminalViewer::BeginOpen() {
    if (!client_) return;
    if (!EnsurePaired()) {
        SetState(TerminalViewerState::Refused, deskhub::ui::kTerminalClosed);
        return;
    }
    client_->Open(config_.passcode, deskhub::ClampTermSize(config_.size), config_.clientName);
}

bool TerminalViewer::EnsurePaired() {
    deskhub::PairingHello hello;
    hello.fingerprint = LoadOrCreateMachineFingerprint();
    hello.clientName = config_.clientName;

    uint8_t buf[deskhub::kMaxDatagram];
    const size_t helloBytes = deskhub::BuildPairingHello(buf, hello);
    if (!helloBytes) return true;

    const uint64_t started = NowUs();
    uint64_t nextHello = started;
    bool sawHost = false;
    uint8_t rx[deskhub::kMaxDatagram];

    while (NowUs() - started < kPairingWaitUs) {
        if (NowUs() >= nextHello) {
            if (!sock_.SendTo(config_.host, buf, helloBytes)) return true;
            nextHello = NowUs() + kPairingRetryUs;
        }

        NetAddr from;
        const int n = sock_.RecvFrom(rx, sizeof(rx), from);
        if (n <= 0) {
            if (!sawHost && NowUs() - started >= kPairingLegacyUs) return true;
            continue;
        }
        if (from != config_.host) continue;

        const std::optional<deskhub::CommonHeader> header =
            deskhub::ParseCommonHeader(std::span<const uint8_t>(rx, size_t(n)));
        if (!header || header->chan != deskhub::Chan::Control) continue;
        if (header->type == deskhub::MsgType::PairingResult) {
            sawHost = true;
            const std::optional<deskhub::PairingResult> result =
                deskhub::ParsePairingResult(deskhub::PayloadOf(std::span<const uint8_t>(rx, size_t(n))));
            if (!result) continue;
            if (result->code == deskhub::PairingResultCode::Accepted) return true;
            if (result->code == deskhub::PairingResultCode::Disabled ||
                result->code == deskhub::PairingResultCode::Refused)
                return false;
        }
    }

    return !sawHost;
}

void TerminalViewer::TryReattach(uint64_t nowUs) {
    if (resumeRetryAtUs_ == 0 || nowUs < resumeRetryAtUs_) return;
    resumeRetryAtUs_ = 0;
    if (lostAtUs_ == 0 || !client_) return;
    if (!deskhub::ClientReconnectStillWorthTrying(nowUs - lostAtUs_,
            deskhub::kTerminalReattachGraceUs)) {
        SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
        return;
    }
    if (client_->CanReattach()) {
        SetState(TerminalViewerState::Reattaching, deskhub::ui::kTerminalReattaching);
        client_->Reattach();
        return;
    }
    resumeRetryAtUs_ = nowUs + deskhub::ClientReconnectBackoffUs(int(resumeAttempts_));
    ++resumeAttempts_;
}

void TerminalViewer::Stop() {
    if (Running()) {
        if (State() == TerminalViewerState::Live) {
            Post([this] {
                if (client_) client_->Close();
            });
        }
        stop_.store(true, std::memory_order_release);
        if (thread_.joinable()) thread_.join();
    }
    stop_.store(true, std::memory_order_release);
    sock_.Close();
    running_.store(false, std::memory_order_release);
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

void TerminalViewer::HandleDatagram(std::span<const uint8_t> datagram) {
    if (!client_) return;
    if (std::optional<std::vector<uint8_t>> message = FeedTermRecord(stream_, datagram)) {
        lastRecvUs_ = NowUs();
        if (lostAtUs_ != 0) {
            lostAtUs_ = 0;
            resumeRetryAtUs_ = 0;
            resumeAttempts_ = 0;
        }
        client_->HandleMessage(*message);
        std::vector<uint8_t> more;
        while (stream_.Next(more)) client_->HandleMessage(more);
    }
}

void TerminalViewer::Loop() {
    uint8_t buf[deskhub::kMaxDatagram];
    while (!stop_.load(std::memory_order_acquire)) {
        RunCommands();
        FlushOutbox();

        NetAddr from;
        const int n = sock_.RecvFrom(buf, sizeof(buf), from);
        const uint64_t nowUs = NowUs();
        if (n > 0) {
            if (from == config_.host) HandleDatagram(std::span<const uint8_t>(buf, size_t(n)));
        } else if (n < 0) {
            SetState(TerminalViewerState::Failed, deskhub::ui::kTerminalUnreachable);
            break;
        }

        if (State() == TerminalViewerState::Live && lastRecvUs_ != 0 &&
            nowUs - lastRecvUs_ > kRecvSilentUs) {
            lostAtUs_ = nowUs;
            lastRecvUs_ = 0;
            if (client_) client_->LinkLost();
            SetState(TerminalViewerState::Reattaching, deskhub::ui::kTerminalReattaching);
            resumeRetryAtUs_ = nowUs + deskhub::ClientReconnectBackoffUs(0);
            resumeAttempts_ = 1;
        }

        TryReattach(nowUs);
    }
    RunCommands();
    FlushOutbox();
}

void TerminalViewer::AcceptFingerprint() {}

void TerminalViewer::RejectFingerprint() {
    Stop();
    SetState(TerminalViewerState::Refused, deskhub::ui::kTerminalClosed);
}

void TerminalViewer::SendRecord(std::span<const uint8_t> message) {
    if (message.empty()) return;
    if (outboxBytes_ + message.size() > kMaxOutboxBytes) return;
    outboxBytes_ += message.size();
    outbox_.emplace_back(message.begin(), message.end());
    FlushOutbox();
}

void TerminalViewer::FlushOutbox() {
    if (!sock_.IsOpen()) return;
    while (!outbox_.empty()) {
        if (!SendTermMessage(sock_, config_.host, outbox_.front())) return;
        outboxBytes_ -= outbox_.front().size();
        outbox_.pop_front();
    }
}

void TerminalViewer::SendBytes(const std::string& bytes) {
    if (bytes.empty() || !Running()) return;
    const auto payload = std::make_shared<const std::string>(bytes);
    Post([this, payload] {
        if (!client_) return;
        client_->SendInput(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(payload->data()), payload->size()));
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
        if (client_) client_->Resize(clamped);
    });
    if (cb_.onRedraw) cb_.onRedraw();
}

}
