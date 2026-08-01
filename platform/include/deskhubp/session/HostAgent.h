#pragma once
#include "deskhub/media/AgentTypes.h"
#include "deskhub/session/Beacon.h"
#include "deskhub/session/SourcePipelineState.h"
#include "deskhubp/net/UdpSocket.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace deskhubp {

using SourcePredicate = std::function<bool(const deskhub::SourcePipelineState&)>;

void SendEncodedFrame(deskhub::SourcePipelineState& st, UdpSocket& sock,
    std::span<const uint8_t> frame, uint64_t timestampUs, bool keyframe);

void LogListeningAddresses(uint16_t port);

std::vector<deskhub::SourcePipelineState*> SelectLiveSources(
    std::span<deskhub::SourcePipelineState* const> pipes, const SourcePredicate& closed,
    const std::function<bool()>& aborted,
    const std::function<void(deskhub::SourcePipelineState&)>& shutdown);

void EndHostSession(deskhub::SourcePipelineState& st, UdpSocket& sock);

struct SourceStatusHooks {
    SourcePredicate closed;
    SourcePredicate zeroCopy;
};

std::vector<deskhub::media::AgentSourceStatus> PublishSourceStatus(
    std::span<deskhub::SourcePipelineState* const> live, deskhub::Beacon& beacon,
    const SourceStatusHooks& hooks);

void LogTransferTotals(std::span<deskhub::SourcePipelineState* const> pipes);

}
