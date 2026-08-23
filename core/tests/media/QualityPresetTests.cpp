#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/ShareTypes.h"
#include "deskhub/media/QualityPreset.h"

#include <cstdio>
#include <cstring>

using deskhub::media::kNativeMaxDim;
using deskhub::media::kQualityPresets;
using deskhub::media::QualityPresetIndex;
using deskhub::media::QualityPresetMaxDim;
using deskhub::media::ShareOptions;

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
    const size_t i = QualityPresetIndex(ShareOptions{}.maxDim);
    Check(kQualityPresets[i].maxDim == ShareOptions{}.maxDim,
        "the share UI can preselect the built-in default instead of silently changing it");
}

void TestUnknownCapFallsBackToTheSafestRung() {
    std::printf("[quality] a cap that is not on the ladder falls back, never to uncapped...\n");
    const size_t i = QualityPresetIndex(4321);
    Check(kQualityPresets[i].maxDim != kNativeMaxDim,
        "an unrecognised cap never resolves to Native");
    Check(i == 0, "it resolves to the smallest rung, which cannot send more than asked for");
}

void TestMaxDimIsTheInverseOfIndex() {
    std::printf("[quality] a combo row maps back to the cap that filled it...\n");
    for (size_t i = 0; i < kQualityPresets.size(); ++i)
        Check(QualityPresetMaxDim(i, 12345) == kQualityPresets[i].maxDim,
            "every row returns its own cap");
    Check(QualityPresetMaxDim(kQualityPresets.size(), 12345) == 12345,
        "an out-of-range selection falls back instead of reading past the table");
}

}

void RunQualityPresetTests() {
    TestEveryPresetIsUsable();
    TestIndexRoundTrips();
    TestSharedDefaultIsOffered();
    TestUnknownCapFallsBackToTheSafestRung();
    TestMaxDimIsTheInverseOfIndex();
}
