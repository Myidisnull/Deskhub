#pragma once
#include "deskhub/protocol/RecordStream.h"
#include "deskhub/session/TerminalSession.h"
#include "deskhubp/net/QuicEndpoint.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/Pty.h"

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace deskhubp {

struct TerminalHostConfig {
    std::string bindIp{};
    uint16_t port = deskhub::kDeskhubPort;
    std::string passcode{};
    std::string shell{};
};

struct TerminalHostCallbacks {
    std::function<void(std::string_view auditLine)> onAudit;
    std::function<void()> onSessionsChanged;
};

class TerminalHost {
public:
    TerminalHost() = default;
    ~TerminalHost();
    TerminalHost(const TerminalHost&) = delete;
    TerminalHost& operator=(const TerminalHost&) = delete;

    bool Start(const TerminalHostConfig& config, const HostIdentity& identity,
        TerminalHostCallbacks callbacks);
    void Stop();

    bool Running() const {
        return running_.load(std::memory_order_acquire);
    }
    bool LastPortInUse() const {
        return endpoint_.LastBindAddrInUse();
    }
    void SetPasscode(std::string passcode);
    void KickSession(uint32_t termId);
    size_t SessionCount() const;
    std::vector<deskhub::TerminalRecord> Sessions() const;
    uint16_t Port() const {
        return config_.port;
    }
    const std::string& BoundIp() const {
        return config_.bindIp;
    }

private:
    struct Shell {
        std::unique_ptr<Pty> pty{};
        QuicConnId conn = 0;
        uint64_t stream = 0;
    };

    void Loop();
    void OnStream(QuicConnId conn, uint64_t stream, std::span<const uint8_t> bytes);
    void HandleMessage(QuicConnId conn, uint64_t stream, std::span<const uint8_t> message);
    void OnConnectionClosed(QuicConnId conn, uint64_t nowUs);
    void PumpShells(uint64_t nowUs);
    void DrainKicks();
    void SendToStream(QuicConnId conn, uint64_t stream, std::span<const uint8_t> message);
    void CloseShell(uint32_t termId, int exitCode, bool tellClient);
    void Audit(uint32_t termId, std::string_view what);
    uint32_t TermIdFor(QuicConnId conn, uint64_t stream) const;

    TerminalHostConfig config_{};
    HostIdentity identity_{};
    TerminalHostCallbacks cb_{};
    QuicEndpoint endpoint_{};
    deskhub::TerminalSessions sessions_{};
    std::map<uint32_t, Shell> shells_{};
    std::vector<uint32_t> kicks_{};
    std::map<uint64_t, deskhub::RecordStream> streams_{};
    mutable std::mutex mutex_{};
    std::thread thread_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
};

}
