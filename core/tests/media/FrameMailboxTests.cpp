#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/FrameMailbox.h"

#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using deskhub::media::FrameMailbox;

namespace {

void TestLatestWins() {
    std::printf("[mailbox] a newer frame replaces the one still waiting...\n");
    FrameMailbox<int> box;
    Check(!box.Put(1).has_value(), "the first frame displaces nothing");
    Check(box.Put(2).value_or(0) == 1, "the second hands back the first");
    Check(box.Put(3).value_or(0) == 2, "the third hands back the second");
    int got = 0;
    Check(box.TakeWait(got), "a frame is available");
    Check(got == 3, "and it is the newest one");
    Check(box.TakeSuperseded() == 2, "the two older frames count as superseded");
    Check(box.TakeSuperseded() == 0, "the counter resets on read");
}

void TestTakeBlocksUntilPut() {
    std::printf("[mailbox] take blocks until a frame arrives...\n");
    FrameMailbox<std::string> box;
    std::string got;
    std::thread taker([&] { Check(box.TakeWait(got), "the blocked take returns a frame"); });
    box.Put("frame");
    taker.join();
    Check(got == "frame", "and it is the frame that was put");
}

void TestCloseUnblocksAndRefuses() {
    std::printf("[mailbox] close unblocks waiters and refuses later frames...\n");
    FrameMailbox<int> box;
    int got = 0;
    std::thread taker([&] { Check(!box.TakeWait(got), "a waiter wakes empty-handed on close"); });
    box.Close();
    taker.join();

    Check(box.Put(7).value_or(0) == 7, "a frame put after close bounces straight back");
    Check(box.TakeSuperseded() == 0, "and is not counted as superseded");
    std::thread after([&] { Check(!box.TakeWait(got), "take after close returns nothing"); });
    after.join();
}

void TestCloseDropsPendingFrame() {
    std::printf("[mailbox] close drops a frame that was never taken...\n");
    FrameMailbox<std::vector<int>> box;
    box.Put({1, 2, 3});
    box.Close();
    std::vector<int> got;
    Check(!box.TakeWait(got), "the pending frame is gone after close");
}

void TestMoveOnlyFrames() {
    std::printf("[mailbox] move-only payloads pass through without copies...\n");
    FrameMailbox<std::unique_ptr<int>> box;
    box.Put(std::make_unique<int>(42));
    std::unique_ptr<int> got;
    Check(box.TakeWait(got), "the frame arrives");
    Check(got && *got == 42, "with its payload intact");
}

}

void RunFrameMailboxTests() {
    TestLatestWins();
    TestTakeBlocksUntilPut();
    TestCloseUnblocksAndRefuses();
    TestCloseDropsPendingFrame();
    TestMoveOnlyFrames();
}
