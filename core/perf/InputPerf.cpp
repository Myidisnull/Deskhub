#include "Perf.h"
#include "PerfHarness.h"

#include "deskhub/input/ClientInputQueue.h"
#include "deskhub/input/InputReceiver.h"
#include "deskhub/input/InputSender.h"
#include "deskhub/protocol/Wire.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

using namespace deskhub;
using namespace deskhub::perf;

namespace {

constexpr size_t kMovesPerRun = 24;
constexpr size_t kWarmUpRuns = 16;
constexpr uint64_t kPollIntervalUs = 8000;
constexpr int32_t kNormalizedRange = 65536;
constexpr int32_t kVkA = 0x41;
constexpr int32_t kScanA = 0x1E;

struct InputRig {
    ClientInputQueue queue{};
    InputSender sender{};
    InputReceiver receiver{};
    std::vector<InputEvent> drained{};
    uint64_t nowUs = 0;
    InputReceiver::ApplyFn apply{};
    InputSender::SendFn deliver{};

    InputRig() {
        sender.SetSessionId(1);
        apply = [](const InputEvent& event) { Consume(event.timestampUs); };
        deliver = [this](std::span<const uint8_t> datagram) {
            receiver.HandlePacket(datagram.subspan(kCommonHeaderSize), apply);
        };
    }

    void DrainAndSend() {
        queue.Drain(nowUs, drained);
        for (const InputEvent& event : drained) sender.Queue(event);
        sender.Flush(nowUs, deliver);
    }
};

}

void RunInputPerf() {
    BeginGroup("input path (queue -> wire -> apply)");

    InputRig mouseRig;
    const std::function<void()> moveBatch = [&] {
        mouseRig.nowUs += kPollIntervalUs;
        for (size_t i = 0; i < kMovesPerRun; ++i)
            mouseRig.queue.MouseMoveAbsolute(int32_t(NextRandom() % kNormalizedRange),
                int32_t(NextRandom() % kNormalizedRange), mouseRig.nowUs);
        mouseRig.DrainAndSend();
    };
    for (size_t i = 0; i < kWarmUpRuns; ++i) moveBatch();
    Measure(Workload{"input/mouse-move-round-trip", "event", kMovesPerRun, 0, 0.2, moveBatch});

    InputRig tapRig;
    const std::function<void()> tap = [&] {
        tapRig.nowUs += kPollIntervalUs;
        tapRig.queue.KeyTap(kVkA, kScanA, tapRig.nowUs);
        tapRig.DrainAndSend();
        tapRig.nowUs += kTapHoldUs;
        tapRig.DrainAndSend();
    };
    for (size_t i = 0; i < kWarmUpRuns; ++i) tap();
    Measure(Workload{"input/key-tap-round-trip", "tap", 1, 0, 0.5, tap});

    InputRig scalingRig;
    MeasureScaling(ScalingWorkload{"input/mouse-move-scaling", "event", 24, 96, 6.0,
        [&](uint64_t units) {
            scalingRig.nowUs += kPollIntervalUs;
            for (uint64_t i = 0; i < units; ++i)
                scalingRig.queue.MouseMoveAbsolute(int32_t(NextRandom() % kNormalizedRange),
                    int32_t(NextRandom() % kNormalizedRange), scalingRig.nowUs);
            scalingRig.DrainAndSend();
        }});
}
