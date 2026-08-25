#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/PcmRing.h"

#include <cstdio>
#include <thread>
#include <vector>

using deskhub::media::AudioFormat;
using deskhub::media::kPcmRingFrames;
using deskhub::media::PcmRing;

namespace {

AudioFormat TinyFormat() {
    AudioFormat format{};
    format.sampleRate = 8000;
    format.channels = 1;
    format.samplesPerFrame = 160;
    return format;
}

std::vector<int16_t> Ramp(size_t count, int16_t first) {
    std::vector<int16_t> pcm(count);
    for (size_t i = 0; i < count; ++i) pcm[i] = int16_t(first + int16_t(i));
    return pcm;
}

void TestClosedRingRefusesEverything() {
    std::printf("[pcmring] a ring nobody opened swallows writes and hands back silence...\n");
    PcmRing ring;
    const std::vector<int16_t> pcm = Ramp(4, 1);
    ring.Put(pcm);
    Check(ring.framesQueued() == 0, "an unopened ring queues nothing");
    Check(ring.framesDropped() == 0, "and counts no drop, because nothing was ever stored");

    int16_t out[4] = {7, 7, 7, 7};
    Check(ring.TakeOrSilence(out, 4) == 0, "a read finds no samples");
    Check(out[0] == 0 && out[3] == 0, "and the caller's buffer is zeroed");
    Check(ring.framesStarved() == 1, "the starve is counted");
}

void TestRoundTripInOrder() {
    std::printf("[pcmring] samples come back in the order they went in...\n");
    PcmRing ring;
    const AudioFormat format = TinyFormat();
    ring.Open(format);

    const size_t frame = format.interleavedSamples();
    ring.Put(Ramp(frame, 100));
    ring.Put(Ramp(frame, 1000));
    Check(ring.framesQueued() == 2, "both frames are queued");

    std::vector<int16_t> out(frame * 2, -1);
    Check(ring.Take(out.data(), out.size()) == frame * 2, "one read drains both");
    Check(out[0] == 100, "the first frame leads");
    Check(out[frame] == 1000, "the second follows");
    Check(ring.framesQueued() == 0, "and the ring is empty again");
    Check(ring.framesDropped() == 0 && ring.framesStarved() == 0, "with nothing lost");
}

void TestPartialReadLeavesTheRest() {
    std::printf("[pcmring] a short read leaves the remainder in place...\n");
    PcmRing ring;
    const AudioFormat format = TinyFormat();
    ring.Open(format);

    const size_t frame = format.interleavedSamples();
    ring.Put(Ramp(frame, 0));

    std::vector<int16_t> head(frame / 2, -1);
    Check(ring.Take(head.data(), head.size()) == frame / 2, "the head comes out");
    Check(head[0] == 0, "starting at the first sample");

    std::vector<int16_t> tail(frame / 2, -1);
    Check(ring.Take(tail.data(), tail.size()) == frame / 2, "the tail follows");
    Check(tail[0] == int16_t(frame / 2), "resuming exactly where the head stopped");
    Check(ring.framesQueued() == 0, "nothing is left");
}

void TestOverflowDropsOldestAndCounts() {
    std::printf("[pcmring] a full ring drops the oldest audio and counts it...\n");
    PcmRing ring;
    const AudioFormat format = TinyFormat();
    ring.Open(format);

    const size_t frame = format.interleavedSamples();
    for (size_t i = 0; i < kPcmRingFrames; ++i) ring.Put(Ramp(frame, int16_t(i)));
    Check(ring.framesQueued() == kPcmRingFrames, "the ring fills to capacity");
    Check(ring.framesDropped() == 0, "with no drop yet");

    ring.Put(Ramp(frame, 99));
    Check(ring.framesQueued() == kPcmRingFrames, "one more frame does not grow it");
    Check(ring.framesDropped() == 1, "the overrun is counted once");

    std::vector<int16_t> out(frame, -1);
    Check(ring.Take(out.data(), out.size()) == frame, "the next read succeeds");
    Check(out[0] == 1, "and the oldest frame is the one that went");
}

void TestWriteLargerThanRingIsRefused() {
    std::printf("[pcmring] a write bigger than the whole ring is refused outright...\n");
    PcmRing ring;
    const AudioFormat format = TinyFormat();
    ring.Open(format);

    const size_t frame = format.interleavedSamples();
    ring.Put(Ramp(frame, 5));
    ring.Put(Ramp(frame * (kPcmRingFrames + 1), 0));
    Check(ring.framesQueued() == 1, "the oversized write never lands");
    Check(ring.framesDropped() == 0, "and it evicts nothing on the way out");
}

void TestTakeOrSilencePadsAndCounts() {
    std::printf("[pcmring] an underrun is padded with silence and counted...\n");
    PcmRing ring;
    const AudioFormat format = TinyFormat();
    ring.Open(format);

    const size_t frame = format.interleavedSamples();
    ring.Put(Ramp(frame, 1));

    std::vector<int16_t> out(frame * 2, -1);
    Check(ring.TakeOrSilence(out.data(), out.size()) == frame, "only the queued half is real");
    Check(out[0] == 1, "the real samples lead");
    Check(out[frame] == 0 && out.back() == 0, "and the shortfall is silence");
    Check(ring.framesStarved() == 1, "the starve is counted once");

    Check(ring.TakeOrSilence(out.data(), out.size()) == 0, "an empty ring gives nothing");
    Check(ring.framesStarved() == 2, "and counts a second starve");
}

void TestReopenClearsBacklog() {
    std::printf("[pcmring] reopening drops whatever the old format left behind...\n");
    PcmRing ring;
    ring.Open(TinyFormat());
    ring.Put(Ramp(TinyFormat().interleavedSamples(), 3));
    Check(ring.framesQueued() == 1, "the old frame is queued");

    ring.Open(AudioFormat{});
    Check(ring.framesQueued() == 0, "the reopened ring starts empty");

    std::vector<int16_t> out(4, -1);
    Check(ring.Take(out.data(), out.size()) == 0, "and yields no stale samples");
}

void TestConcurrentWriterAndReader() {
    std::printf("[pcmring] a writer and a reader can run at once without losing order...\n");
    PcmRing ring;
    const AudioFormat format = TinyFormat();
    ring.Open(format);

    const size_t frame = format.interleavedSamples();
    constexpr size_t kRounds = 400;

    std::thread writer([&] {
        for (size_t i = 0; i < kRounds; ++i) ring.Put(Ramp(frame, 0));
    });

    size_t taken = 0;
    std::vector<int16_t> out(frame, -1);
    for (size_t i = 0; i < kRounds * 4; ++i) taken += ring.Take(out.data(), out.size());
    writer.join();
    while (ring.framesQueued() != 0) taken += ring.Take(out.data(), out.size());

    Check(taken % frame == 0, "reads never split a frame");
    Check(taken / frame + ring.framesDropped() == kRounds,
        "every frame written was either read or counted as dropped");
    Check(ring.framesQueued() == 0, "and the ring ends drained");
}

}

void RunPcmRingTests() {
    TestClosedRingRefusesEverything();
    TestRoundTripInOrder();
    TestPartialReadLeavesTheRest();
    TestOverflowDropsOldestAndCounts();
    TestWriteLargerThanRingIsRefused();
    TestTakeOrSilencePadsAndCounts();
    TestReopenClearsBacklog();
    TestConcurrentWriterAndReader();
}
