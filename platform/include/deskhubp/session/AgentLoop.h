#pragma once
#include "deskhubp/session/HostEngine.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using AgentSource = deskhub::media::ShareSource;
using AgentOptions = deskhub::media::AgentOptions;
using AgentSourceStatus = deskhub::media::AgentSourceStatus;

using PairingRequest = deskhubp::PairingRequest;

class AgentLoop {
public:
    AgentLoop() = default;
    AgentLoop(const AgentLoop&) = delete;
    AgentLoop& operator=(const AgentLoop&) = delete;

    bool Start(const std::vector<AgentSource>& sources, const AgentOptions& opt);

    void Stop() {
        engine_.Stop();
    }

    void OfferAudio(std::span<const int16_t> pcm) {
        engine_.OfferAudio(pcm);
    }

    bool audioRunning() const {
        return engine_.audioRunning();
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

    std::optional<std::string> TakeRemoteClipboard() {
        return engine_.TakeRemoteClipboard();
    }

    void PushPairingRequest(PairingRequest request) {
        const std::lock_guard<std::mutex> lock(pairingMutex_);
        pairingRequests_.push_back(std::move(request));
    }

    std::vector<PairingRequest> TakePairingRequests(size_t maxCount = SIZE_MAX) {
        const std::lock_guard<std::mutex> lock(pairingMutex_);
        std::vector<PairingRequest> out;
        if (maxCount >= pairingRequests_.size()) {
            out.swap(pairingRequests_);
            return out;
        }
        const auto split = pairingRequests_.begin() + ptrdiff_t(maxCount);
        out.assign(pairingRequests_.begin(), split);
        pairingRequests_.erase(pairingRequests_.begin(), split);
        return out;
    }

    void AnswerPairing(uint64_t addrPacked, bool allowed) {
        engine_.AnswerPairingRequest(addrPacked, allowed);
    }

    deskhubp::SessionTransport& Socket() {
        return engine_.socket();
    }

    void SetTerminal(deskhubp::TerminalHost* terminal) {
        engine_.SetTerminal(terminal);
    }

private:
    deskhubp::HostEngine engine_;
    std::mutex pairingMutex_;
    std::vector<PairingRequest> pairingRequests_;
};
