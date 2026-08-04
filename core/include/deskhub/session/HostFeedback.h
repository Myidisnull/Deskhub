#pragma once
#include "deskhub/control/QualityLadder.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/SourcePipelineState.h"

#include <cstdint>
#include <functional>

namespace deskhub {

struct FeedbackHooks {
    std::function<bool(uint32_t bitrateBps)> setEncoderBitrate;
    std::function<StreamSize(const QualityStep& prev, const QualityStep& next)> applyQualityStep;
};

struct FeedbackOutcome {
    bool fecToggled = false;
    bool fecEnabled = false;

    bool bitrateChanged = false;
    uint32_t previousBitrateBps = 0;
    uint32_t bitrateBps = 0;

    bool qualityChanged = false;
    QualityStep previousStep;
    QualityStep step;
    StreamSize size;
};

FeedbackOutcome ApplyFeedback(SourcePipelineState& st, const Feedback& fb, uint64_t nowUs,
    const FeedbackHooks& hooks);

void RespondToNack(SourcePipelineState& st, uint32_t frameId, std::span<const uint16_t> indices,
    const std::function<void(std::span<const uint8_t>)>& sendToRequester);

void ForgetViewers(SourcePipelineState& st);

}
