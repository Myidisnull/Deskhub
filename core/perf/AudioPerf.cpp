#include "Perf.h"
#include "PerfHarness.h"

#include "deskhub/media/AudioTypes.h"
#include "deskhub/media/PcmRing.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace deskhub;
using namespace deskhub::perf;

namespace {

constexpr size_t kFrameSamples = media::kAudioSamplesPerFrameInterleaved;
constexpr size_t kFrameBytes = kFrameSamples * sizeof(int16_t);

std::vector<int16_t> MakePcmFrame() {
    std::vector<int16_t> pcm(kFrameSamples);
    for (int16_t& sample : pcm) sample = int16_t(NextRandom());
    return pcm;
}

}

void RunAudioPerf() {
    BeginGroup("audio playback ring");

    const std::vector<int16_t> frame = MakePcmFrame();
    std::vector<int16_t> playback(kFrameSamples);

    media::PcmRing ring;
    ring.Open(media::AudioFormat{});
    Measure(Workload{"audio/pcm-ring-put-take", "frame", 1, kFrameBytes, 0.0, [&] {
                         ring.Put(frame);
                         Consume(ring.TakeOrSilence(playback.data(), kFrameSamples));
                     }});

    media::PcmRing fullRing;
    fullRing.Open(media::AudioFormat{});
    for (size_t i = 0; i < media::kPcmRingFrames; ++i) fullRing.Put(frame);
    Measure(Workload{"audio/pcm-ring-overflow-drop", "frame", 1, kFrameBytes, 0.0, [&] {
                         fullRing.Put(frame);
                         Consume(fullRing.framesDropped());
                     }});

    media::PcmRing scalingRing;
    scalingRing.Open(media::AudioFormat{});
    MeasureScaling(ScalingWorkload{"audio/pcm-ring-scaling", "frame", 16, 64, 6.0,
        [&](uint64_t units) {
            for (uint64_t i = 0; i < units; ++i) {
                scalingRing.Put(frame);
                Consume(scalingRing.TakeOrSilence(playback.data(), kFrameSamples));
            }
        }});
}
