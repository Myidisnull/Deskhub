#pragma once
#include "deskhub/net/TrustStore.h"
#include "deskhub/protocol/RecordStream.h"
#include "deskhub/session/TerminalClient.h"
#include "deskhub/terminal/KeyEncoder.h"
#include "deskhub/terminal/Screen.h"
#include "deskhubp/net/QuicEndpoint.h"
#include "deskhubp/session/AuthNegotiation.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace deskhubp {

enum class TerminalViewerState : uint8_t {
    Idle = 0,
    Connecting = 1,
    Deciding = 2,
    Opening = 3,
    Live = 4,
    Reattaching = 5,
    Refused = 6,
    Failed = 7,
    Ended = 8,
};

struct TerminalViewerConfig {
    NetAddr host{};
    std::string hostLabel{};
    std::string passcode{};
    std::string clientName{};
    deskhub::TermSize size{};
};

struct TerminalViewerCallbacks {
    std::function<void()> onRedraw;
    std::function<void(TerminalViewerState, std::string_view message)> onState;
    std::function<void(deskhub::TrustVerdict, std::string_view fingerprint)> onTrustAsked;
};

struct TerminalSnapshot {
    deskhub::TermSize size{};
    std::vector<deskhub::term::Cell> cells{};
    deskhub::term::CursorState cursor{};
    std::string title{};
    uint64_t revision = 0;
    size_t scrollbackRows = 0;
    size_t scrollOffset = 0;

    const deskhub::term::Cell& At(uint16_t row, uint16_t col) const;
};

class TerminalViewer {
public:
    TerminalViewer() = default;
    ~TerminalViewer();
    TerminalViewer(const TerminalViewer&) = delete;
    TerminalViewer& operator=(const TerminalViewer&) = delete;

    bool Start(const TerminalViewerConfig& config, TerminalViewerCallbacks callbacks);
    void Stop();

    void AcceptFingerprint();
    void RejectFingerprint();

    void SendKey(const deskhub::term::TermKeyEvent& key);
    void SendText(std::string_view text);
    void Paste(std::string_view text);
    void Resize(deskhub::TermSize size);

    TerminalViewerState State() const {
        return state_.load(std::memory_order_acquire);
    }
    bool Running() const {
        return running_.load(std::memory_order_acquire);
    }
    std::string Message() const;
    std::string Fingerprint() const;
    deskhub::TrustVerdict Verdict() const {
        return verdict_;
    }
    TerminalSnapshot Snapshot(size_t scrollOffset = 0) const;

private:
    void Loop();
    void OnConnected(QuicConnId conn);
    void OnStream(std::span<const uint8_t> bytes);
    void SendRecord(std::span<const uint8_t> message);
    void SendBytes(const std::string& bytes);
    void SetState(TerminalViewerState state, std::string_view message);
    void Post(std::function<void()> command);
    void RunCommands();
    void RememberIfPasscodeProvedIt();
    void BeginAuth();
    bool HandleAuth(std::span<const uint8_t> message);
    deskhub::term::TerminalModes CurrentModes() const;

    TerminalViewerConfig config_{};
    TerminalViewerCallbacks cb_{};
    QuicEndpoint endpoint_{};
    QuicConnId conn_ = 0;
    deskhub::RecordStream framer_{};
    std::unique_ptr<deskhub::TerminalClient> client_{};
    std::unique_ptr<ClientAuth> auth_{};
    deskhub::term::Screen screen_{};
    deskhub::Fingerprint fingerprint_{};
    deskhub::TrustVerdict verdict_ = deskhub::TrustVerdict::Unknown;
    std::string message_{};
    std::vector<uint8_t> outbox_{};

    mutable std::mutex mutex_{};
    std::mutex commandMutex_{};
    std::vector<std::function<void()>> commands_{};
    std::thread thread_{};
    std::atomic<TerminalViewerState> state_{TerminalViewerState::Idle};
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> autoTrustPending_{false};
};

}
