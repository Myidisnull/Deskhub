#include "deskhubp/session/TerminalHost.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "deskhub/protocol/Wire.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"

namespace deskhubp {

namespace {

constexpr uint32_t kPumpWaitUs = 2'000;
constexpr uint32_t kPtyWaitMs = 0;

}

TerminalHost::~TerminalHost() {
    Stop();
}

bool TerminalHost::Start(SessionTransport& sock, std::string shell,
    TerminalHostCallbacks callbacks) {
    if (Running()) return false;
    if (!sock.IsOpen()) return false;

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        sock_ = &sock;
        shell_ = std::move(shell);
        cb_ = std::move(callbacks);
        sessions_.SetConnectionAuthenticated(true);
        sessions_.SetSharing(true);
    }
    {
        const std::lock_guard<std::mutex> lock(goneMutex_);
        gone_.clear();
    }

    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { Loop(); });
    LOGI("terminal host: sharing a shell on the session port");
    return true;
}

void TerminalHost::Stop() {
    if (!Running()) return;
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    running_.store(false, std::memory_order_release);

    const std::lock_guard<std::mutex> lock(mutex_);
    shells_.clear();
    kicks_.clear();
    sessions_.SetSharing(false);
    sock_ = nullptr;
    LOGI("terminal host: stopped sharing");
}

void TerminalHost::KickSession(uint32_t termId) {
    const std::lock_guard<std::mutex> lock(mutex_);
    kicks_.push_back(termId);
}

size_t TerminalHost::SessionCount() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.Count();
}

std::vector<deskhub::TerminalRecord> TerminalHost::Sessions() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.Records();
}

void TerminalHost::Loop() {
    while (!stop_.load(std::memory_order_acquire)) {
        const uint64_t nowUs = NowUs();
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            DrainGone(nowUs);
            DrainKicks();
            PumpShells();
            for (uint32_t id : sessions_.Expire(nowUs)) {
                shells_.erase(id);
                LOGI("terminal host: gave up on detached session %u", unsigned(id));
            }
        }
        SleepUs(kPumpWaitUs);
    }
}

void TerminalHost::SendToPeer(const NetAddr& peer, std::span<const uint8_t> message) {
    if (sock_ != nullptr) sock_->SendRecord(peer, message);
}

uint32_t TerminalHost::TermIdFor(const NetAddr& peer) const {
    for (const auto& [id, shell] : shells_)
        if (shell.peer == peer) return id;
    return 0;
}

void TerminalHost::HandleMessage(const NetAddr& from, std::span<const uint8_t> message) {
    if (!Running()) return;
    const std::optional<deskhub::CommonHeader> header = deskhub::ParseCommonHeader(message);
    if (!header || header->chan != deskhub::Chan::Terminal) return;
    const std::span<const uint8_t> payload = deskhub::PayloadOf(message);
    const uint64_t nowUs = NowUs();

    const std::lock_guard<std::mutex> lock(mutex_);
    if (sock_ == nullptr) return;

    if (header->type == deskhub::MsgType::TermOpen) {
        const std::optional<deskhub::TermOpen> request = deskhub::ParseTermOpen(payload);
        if (!request) return;

        deskhub::TerminalOpenRequest full;
        full.message = *request;
        full.endpoint = from.ToString();
        std::string peerName;
        sock_->PeerAuth(from, full.fingerprint, peerName);

        deskhub::TermOpenAck ack = sessions_.Open(full, nowUs);
        if (ack.reason == deskhub::TermReason::Accepted && !ack.resumed) {
            Shell shell;
            shell.pty = std::make_unique<Pty>();
            shell.peer = from;
            if (!shell.pty->Start(shell_, request->size)) {
                sessions_.Close(ack.termId);
                ack = deskhub::TermOpenAck{};
                ack.reason = deskhub::TermReason::TooManySessions;
            } else {
                shells_.emplace(ack.termId, std::move(shell));
                Audit(ack.termId, "opened");
            }
        } else if (ack.reason == deskhub::TermReason::Accepted && ack.resumed) {
            const auto at = shells_.find(ack.termId);
            if (at == shells_.end()) {
                sessions_.Close(ack.termId);
                ack = deskhub::TermOpenAck{};
                ack.reason = deskhub::TermReason::NoSuchSession;
            } else {
                at->second.peer = from;
                at->second.pty->Resize(request->size);
                Audit(ack.termId, "reattached");
            }
        }

        std::vector<uint8_t> out(deskhub::kMaxDatagram);
        out.resize(deskhub::BuildTermOpenAck(out, ack));
        SendToPeer(from, out);
        if (cb_.onSessionsChanged) cb_.onSessionsChanged();
        return;
    }

    const uint32_t termId = header->sessionId != 0 ? header->sessionId : TermIdFor(from);
    const auto shell = shells_.find(termId);
    if (shell == shells_.end() || !(shell->second.peer == from)) return;

    switch (header->type) {
        case deskhub::MsgType::TermData:
            if (!payload.empty()) shell->second.pty->Write(payload);
            return;
        case deskhub::MsgType::TermResize:
            if (const std::optional<deskhub::TermSize> size = deskhub::ParseTermResize(payload)) {
                sessions_.Resize(termId, *size);
                shell->second.pty->Resize(*size);
            }
            return;
        case deskhub::MsgType::TermClose:
            CloseShell(termId, 0, false);
            return;
        default: return;
    }
}

void TerminalHost::OnPeerGone(const NetAddr& peer) {
    if (!Running()) return;
    const std::lock_guard<std::mutex> lock(goneMutex_);
    gone_.push_back(peer);
}

void TerminalHost::DrainGone(uint64_t nowUs) {
    std::vector<NetAddr> gone;
    {
        const std::lock_guard<std::mutex> lock(goneMutex_);
        gone.swap(gone_);
    }
    for (const NetAddr& peer : gone) {
        bool changed = false;
        for (auto& [termId, shell] : shells_) {
            if (!(shell.peer == peer)) continue;
            sessions_.Detach(termId, nowUs);
            Audit(termId, "detached");
            changed = true;
        }
        if (changed && cb_.onSessionsChanged) cb_.onSessionsChanged();
    }
}

void TerminalHost::PumpShells() {
    std::vector<uint32_t> finished;
    std::vector<uint8_t> chunk(kPtyReadChunk);
    std::vector<uint8_t> message(deskhub::kMaxRecordSize);

    for (auto& [termId, shell] : shells_) {
        for (int round = 0; round < 8; ++round) {
            const int got = shell.pty->Read(chunk.data(), chunk.size(), kPtyWaitMs);
            if (got < 0) {
                finished.push_back(termId);
                break;
            }
            if (got == 0) break;
            size_t at = 0;
            while (at < size_t(got)) {
                const size_t take = std::min<size_t>(deskhub::kMaxTermDataBytes, size_t(got) - at);
                const size_t written = deskhub::BuildTermData(message, termId,
                    std::span<const uint8_t>(chunk.data() + at, take));
                if (written == 0) break;
                SendToPeer(shell.peer, std::span<const uint8_t>(message.data(), written));
                at += take;
            }
        }
    }

    for (uint32_t termId : finished) {
        const auto at = shells_.find(termId);
        const int code = at == shells_.end() ? 0 : at->second.pty->ExitCode();
        CloseShell(termId, code, true);
    }
}

void TerminalHost::DrainKicks() {
    if (kicks_.empty()) return;
    const std::vector<uint32_t> kicks = std::move(kicks_);
    kicks_.clear();
    for (uint32_t termId : kicks) CloseShell(termId, 0, true);
}

void TerminalHost::CloseShell(uint32_t termId, int exitCode, bool tellClient) {
    const auto at = shells_.find(termId);
    if (at == shells_.end()) return;
    if (tellClient) {
        std::vector<uint8_t> out(deskhub::kMaxDatagram);
        out.resize(deskhub::BuildTermExit(out, termId, exitCode));
        SendToPeer(at->second.peer, out);
    }
    Audit(termId, "closed");
    shells_.erase(at);
    sessions_.Close(termId);
    if (cb_.onSessionsChanged) cb_.onSessionsChanged();
}

void TerminalHost::Audit(uint32_t termId, std::string_view what) {
    const deskhub::TerminalRecord* record = sessions_.Find(termId);
    if (record == nullptr) return;
    const std::string line = deskhub::TerminalAuditLine(*record, what);
    LOGI("%s", line.c_str());
    if (cb_.onAudit) cb_.onAudit(line);
}

}
