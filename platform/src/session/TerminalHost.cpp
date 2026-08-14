#include "deskhubp/session/TerminalHost.h"

#include <algorithm>
#include <vector>

#include "deskhub/net/BindAddress.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"

namespace deskhubp {

namespace {

constexpr uint32_t kPollWaitMs = 5;
constexpr uint32_t kPtyWaitMs = 0;

uint64_t StreamKey(QuicConnId conn, uint64_t stream) {
    return conn ^ (stream << 48);
}

}

TerminalHost::~TerminalHost() {
    Stop();
}

bool TerminalHost::Start(const TerminalHostConfig& config, const HostIdentity& identity,
    TerminalHostCallbacks callbacks) {
    if (Running()) return false;
    if (!identity.Valid()) {
        LOGE("terminal host: no host identity, cannot offer an encrypted shell");
        return false;
    }

    config_ = config;
    identity_ = identity;
    cb_ = std::move(callbacks);
    sessions_.SetPasscode(config_.passcode);
    sessions_.SetSharing(true);

    QuicSettings settings;
    settings.certPemPath = identity_.certPath;
    settings.keyPemPath = identity_.keyPath;

    QuicCallbacks hooks;
    hooks.onStream = [this](QuicConnId conn, uint64_t stream, std::span<const uint8_t> bytes,
                         bool) { OnStream(conn, stream, bytes); };
    hooks.onClosed = [this](QuicConnId conn, const NetAddr&) {
        OnConnectionClosed(conn, NowUs());
    };

    if (!endpoint_.Listen(settings, config_.bindIp, config_.port, std::move(hooks))) {
        sessions_.SetSharing(false);
        return false;
    }

    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { Loop(); });
    LOGI("terminal host: sharing a shell on %s:%u",
        config_.bindIp.empty() ? "0.0.0.0" : config_.bindIp.c_str(), unsigned(config_.port));
    return true;
}

void TerminalHost::Stop() {
    if (!Running()) return;
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    running_.store(false, std::memory_order_release);

    const std::lock_guard<std::mutex> lock(mutex_);
    shells_.clear();
    streams_.clear();
    sessions_.SetSharing(false);
    endpoint_.Close();
    LOGI("terminal host: stopped sharing");
}

void TerminalHost::SetPasscode(std::string passcode) {
    const std::lock_guard<std::mutex> lock(mutex_);
    config_.passcode = passcode;
    sessions_.SetPasscode(std::move(passcode));
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
        endpoint_.Poll(NowUs(), kPollWaitMs);
        const uint64_t nowUs = NowUs();
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            PumpShells(nowUs);
            for (uint32_t id : sessions_.Expire(nowUs)) {
                shells_.erase(id);
                LOGI("terminal host: gave up on detached session %u", unsigned(id));
            }
        }
    }
}

void TerminalHost::OnStream(QuicConnId conn, uint64_t stream, std::span<const uint8_t> bytes) {
    const std::lock_guard<std::mutex> lock(mutex_);
    deskhub::RecordStream& framer = streams_[StreamKey(conn, stream)];
    framer.Append(bytes);
    std::vector<uint8_t> message;
    while (framer.Next(message)) HandleMessage(conn, stream, message);
    if (framer.Failed()) {
        LOGW("terminal host: a client sent a malformed stream, closing it");
        endpoint_.CloseConnection(conn, 1, "bad framing");
    }
}

void TerminalHost::SendToStream(QuicConnId conn, uint64_t stream,
    std::span<const uint8_t> message) {
    std::vector<uint8_t> record(deskhub::kRecordPrefixSize + message.size());
    const size_t written = deskhub::BuildRecord(record, message);
    if (written == 0) return;
    endpoint_.SendStream(conn, stream, std::span<const uint8_t>(record.data(), written));
}

uint32_t TerminalHost::TermIdFor(QuicConnId conn, uint64_t stream) const {
    for (const auto& [id, shell] : shells_)
        if (shell.conn == conn && shell.stream == stream) return id;
    return 0;
}

void TerminalHost::HandleMessage(QuicConnId conn, uint64_t stream,
    std::span<const uint8_t> message) {
    const std::optional<deskhub::CommonHeader> header = deskhub::ParseCommonHeader(message);
    if (!header || header->chan != deskhub::Chan::Terminal) return;
    const std::span<const uint8_t> payload = deskhub::PayloadOf(message);
    const uint64_t nowUs = NowUs();

    if (header->type == deskhub::MsgType::TermOpen) {
        const std::optional<deskhub::TermOpen> request = deskhub::ParseTermOpen(payload);
        if (!request) return;

        deskhub::TerminalOpenRequest full;
        full.message = *request;
        full.endpoint = NetAddr::Unpack(conn).ToString();
        if (const std::optional<deskhub::Fingerprint> fp = endpoint_.PeerFingerprint(conn))
            full.fingerprint = *fp;

        deskhub::TermOpenAck ack = sessions_.Open(full, nowUs);
        if (ack.reason == deskhub::TermReason::Accepted && !ack.resumed) {
            Shell shell;
            shell.pty = std::make_unique<Pty>();
            shell.conn = conn;
            shell.stream = stream;
            if (!shell.pty->Start(config_.shell, request->size)) {
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
                at->second.conn = conn;
                at->second.stream = stream;
                at->second.pty->Resize(request->size);
                Audit(ack.termId, "reattached");
            }
        }

        std::vector<uint8_t> out(deskhub::kMaxDatagram);
        out.resize(deskhub::BuildTermOpenAck(out, ack));
        SendToStream(conn, stream, out);
        if (cb_.onSessionsChanged) cb_.onSessionsChanged();
        return;
    }

    const uint32_t termId = header->sessionId != 0 ? header->sessionId : TermIdFor(conn, stream);
    const auto shell = shells_.find(termId);
    if (shell == shells_.end() || shell->second.conn != conn) return;

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

void TerminalHost::PumpShells(uint64_t nowUs) {
    (void)nowUs;
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
                SendToStream(shell.conn, shell.stream,
                    std::span<const uint8_t>(message.data(), written));
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

void TerminalHost::CloseShell(uint32_t termId, int exitCode, bool tellClient) {
    const auto at = shells_.find(termId);
    if (at == shells_.end()) return;
    if (tellClient) {
        std::vector<uint8_t> out(deskhub::kMaxDatagram);
        out.resize(deskhub::BuildTermExit(out, termId, exitCode));
        SendToStream(at->second.conn, at->second.stream, out);
    }
    Audit(termId, "closed");
    shells_.erase(at);
    sessions_.Close(termId);
    if (cb_.onSessionsChanged) cb_.onSessionsChanged();
}

void TerminalHost::OnConnectionClosed(QuicConnId conn, uint64_t nowUs) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [termId, shell] : shells_) {
        if (shell.conn != conn) continue;
        sessions_.Detach(termId, nowUs);
        Audit(termId, "detached");
    }
    for (auto it = streams_.begin(); it != streams_.end();) {
        if ((it->first ^ (it->first >> 48 << 48)) == conn) it = streams_.erase(it);
        else ++it;
    }
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
