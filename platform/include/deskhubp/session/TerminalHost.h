#pragma once
#include "deskhub/session/TerminalSession.h"
#include "deskhubp/net/SessionTransport.h"
#include "deskhubp/system/Pty.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace deskhubp {

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

    bool Start(SessionTransport& sock, std::string shell, TerminalHostCallbacks callbacks);
    void Stop();

    bool Running() const {
        return running_.load(std::memory_order_acquire);
    }

    void HandleMessage(const NetAddr& from, std::span<const uint8_t> message);
    void OnPeerGone(const NetAddr& peer);

    void KickSession(uint32_t termId);
    size_t SessionCount() const;
    std::vector<deskhub::TerminalRecord> Sessions() const;

private:
    struct Shell {
        std::unique_ptr<Pty> pty{};
        NetAddr peer{};
    };

    void Loop();
    void PumpShells();
    void DrainKicks();
    void DrainGone(uint64_t nowUs);
    void CloseShell(uint32_t termId, int exitCode, bool tellClient);
    void Audit(uint32_t termId, std::string_view what);
    void SendToPeer(const NetAddr& peer, std::span<const uint8_t> message);
    uint32_t TermIdFor(const NetAddr& peer) const;

    SessionTransport* sock_ = nullptr;
    std::string shell_{};
    TerminalHostCallbacks cb_{};
    deskhub::TerminalSessions sessions_{};
    std::map<uint32_t, Shell> shells_{};
    std::vector<uint32_t> kicks_{};
    std::vector<NetAddr> gone_{};
    mutable std::mutex mutex_{};
    std::mutex goneMutex_{};
    std::thread thread_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
};

}
