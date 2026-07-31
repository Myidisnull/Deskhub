#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/AgentTypes.h"
#include "deskhub/media/QualityPreset.h"

#include <cstdio>
#include <cstring>

using deskhub::media::AgentOptions;
using deskhub::media::kNativeMaxDim;
using deskhub::media::kQualityPresets;
using deskhub::media::QualityPresetIndex;

namespace {

void TestEveryPresetIsUsable() {
    std::printf("[quality] every rung the share UI offers is labelled and ordered...\n");
    for (const auto& p : kQualityPresets)
        Check(p.label && std::strlen(p.label) > 0, "each preset carries a label");

    for (size_t i = 1; i + 1 < kQualityPresets.size(); ++i)
        Check(kQualityPresets[i].maxDim > kQualityPresets[i - 1].maxDim,
            "the capped rungs climb");

    Check(kQualityPresets.back().maxDim == kNativeMaxDim,
        "the last rung is the uncapped one, so a bigger number never means uncapped");
}

void TestIndexRoundTrips() {
    std::printf("[quality] a cap maps back to the rung that produced it...\n");
    for (size_t i = 0; i < kQualityPresets.size(); ++i)
        Check(QualityPresetIndex(kQualityPresets[i].maxDim) == i,
            "every preset finds itself");
}

void TestSharedDefaultIsOffered() {
    std::printf("[quality] the default every host starts from is one of the rungs...\n");
    const size_t i = QualityPresetIndex(AgentOptions{}.maxDim);
    Check(kQualityPresets[i].maxDim == AgentOptions{}.maxDim,
        "the share UI can preselect the built-in default instead of silently changing it");
}

void TestUnknownCapFallsBackToTheSafestRung() {
    std::printf("[quality] a cap that is not on the ladder falls back, never to uncapped...\n");
    const size_t i = QualityPresetIndex(4321);
    Check(kQualityPresets[i].maxDim != kNativeMaxDim,
        "an unrecognised cap never resolves to Native");
    Check(i == 0, "it resolves to the smallest rung, which cannot send more than asked for");
}

}

void RunQualityPresetTests() {
    TestEveryPresetIsUsable();
    TestIndexRoundTrips();
    TestSharedDefaultIsOffered();
    TestUnknownCapFallsBackToTheSafestRung();
}
