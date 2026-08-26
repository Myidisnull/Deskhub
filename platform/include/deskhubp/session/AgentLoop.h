#pragma once
#include "deskhubp/session/HostEngine.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using AgentSource = deskhub::media::ShareSource;
using AgentOptions = deskhub::media::AgentOptions;
using AgentSourceStatus = deskhub::media::AgentSourceStatus;

class AgentLoop {
public:
    AgentLoop() = default;
    AgentLoop(const AgentLoop&) = delete;
    AgentLoop& operator=(const AgentLoop&) = delete;

    bool Start(const std::vector<AgentSource>& sources, const AgentOptions& opt);

    bool StartFilesOnly(const AgentOptions& opt) {
        AgentOptions filesOpt = opt;
        filesOpt.acceptFiles = true;
        deskhubp::HostEnginePolicy policy;
        return engine_.Start({}, filesOpt, std::move(policy));
    }

    void Stop() {
        engine_.Stop();
    }

    void StopSource(uint8_t sourceId) {
        engine_.RequestStopSource(sourceId);
    }

    void KickViewer(uint8_t sourceId, uint64_t addrPacked) {
        engine_.RequestKickViewer(sourceId, addrPacked);
    }

    bool running() const {
        return engine_.running();
    }

    bool audioRunning() const {
        return engine_.audioRunning();
    }

    std::vector<AgentSourceStatus> Status() {
        return engine_.Status();
    }

    std::string LastError() {
        return engine_.LastError();
    }

    std::string BindWarning() {
        return engine_.BindWarning();
    }

    void OfferLocalClipboard(std::string text) {
        engine_.OfferLocalClipboard(std::move(text));
    }

    void OfferAudio(std::span<const int16_t> pcm) {
        engine_.OfferAudio(pcm);
    }

    std::optional<std::string> TakeRemoteClipboard() {
        return engine_.TakeRemoteClipboard();
    }

    deskhubp::TerminalHost* Terminal() {
        return engine_.terminal();
    }

    const deskhubp::TerminalHost* Terminal() const {
        return engine_.terminal();
    }

    bool TerminalSharing() const {
        const deskhubp::TerminalHost* term = engine_.terminal();
        return term != nullptr && term->Running();
    }

    std::vector<deskhub::TerminalRecord> TerminalSessions() const {
        const deskhubp::TerminalHost* term = engine_.terminal();
        if (!term) return {};
        return term->Sessions();
    }

    void KickShell(uint32_t termId) {
        if (deskhubp::TerminalHost* term = engine_.terminal()) term->KickSession(termId);
    }

    void StopTerminalShare() {
        if (deskhubp::TerminalHost* term = engine_.terminal()) term->Stop();
    }

    bool StopAndAttachShell(uint32_t termId) {
        deskhubp::TerminalHost* term = engine_.terminal();
        if (!term) return false;
        return term->AttachLocal(termId);
    }

private:
    deskhubp::HostEngine engine_;
};
