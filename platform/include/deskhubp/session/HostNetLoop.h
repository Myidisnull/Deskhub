#pragma once
#include "deskhub/control/QualityLadder.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/session/Beacon.h"
#include "deskhub/session/HostSession.h"
#include "deskhub/session/SourcePipelineState.h"
#include "deskhubp/net/UdpSocket.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace deskhubp {

struct HostSessionHooks {
    std::function<void(std::span<const uint8_t>)> send;
    std::function<void(uint64_t addrPacked, std::span<const uint8_t>)> sendToAddr;
    std::function<void(std::span<const uint8_t>)> sendToRequester;
    std::function<void(std::string_view text)> applyClipboard;
    std::function<deskhub::StreamSize()> retarget;
    std::function<void(const deskhub::InputEvent&)> applyInput;
    std::function<void()> releaseInput;
    std::function<bool(uint32_t bitrateBps)> setEncoderBitrate;
    std::function<deskhub::StreamSize(const deskhub::QualityStep& prev,
        const deskhub::QualityStep& next)>
        applyQualityStep;

    uint32_t fps = 60;
};

deskhub::HostCallbacks MakeHostCallbacks(deskhub::SourcePipelineState& st,
    HostSessionHooks hooks);

struct HostSourceHooks {
    std::function<bool(const deskhub::SourcePipelineState&)> closed;
    std::function<void(deskhub::SourcePipelineState&)> shutdown;
    std::function<void(deskhub::SourcePipelineState&, uint64_t nowUs)> flush;
    std::function<uint64_t(const deskhub::SourcePipelineState&)> inputSkipped;
    std::function<uint32_t(deskhub::SourcePipelineState&)> takeIdleFrames;
    std::function<bool(const deskhub::SourcePipelineState&)> zeroCopy;
};

struct HostNetLoopHooks {
    std::function<bool()> stopped;
    std::function<void()> onTick;
    std::function<void()> publishStatus;
    std::function<void()> onSocketError;
    std::function<void(const NetAddr& from, std::span<const uint8_t> datagram)> onFile;

    HostSourceHooks source;
    uint32_t fallbackFps = 60;
};

void RunHostNetLoop(UdpSocket& sock, deskhub::Beacon& beacon,
    std::span<deskhub::SourcePipelineState* const> live, const HostNetLoopHooks& hooks);

}
