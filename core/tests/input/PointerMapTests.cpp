#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/input/InputApplier.h"
#include "deskhub/input/PointerMap.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

void TestClamp() {
    std::printf("[pointer] out-of-range normalized coordinates clamp to the axis...\n");
    Check(ClampAbsCoord(-1) == 0, "negative clamps to 0");
    Check(ClampAbsCoord(0) == 0, "0 passes through");
    Check(ClampAbsCoord(kAbsCoordMax) == kAbsCoordMax, "the max passes through");
    Check(ClampAbsCoord(kAbsCoordMax + 1) == kAbsCoordMax, "over the max clamps down");
}

void TestPixelMapping() {
    std::printf("[pointer] normalized maps onto a rectangle and back...\n");
    Check(AbsCoordToPixel(0, 100, 1920) == 100, "0 lands on the left edge");
    Check(AbsCoordToPixel(kAbsCoordMax, 100, 1920) == 100 + 1920, "the max lands on the far edge");
    Check(AbsCoordToPixel(kAbsCoordMax / 2, 0, 1920) == 959, "the midpoint lands mid-screen");
    Check(AbsCoordToPixel(30000, 0, 0) == 0, "an empty extent collapses to the origin");
    Check(AbsCoordToPixel(-5, 40, 800) == 40, "a negative input clamps before mapping");

    Check(AxisToAbsCoord(100, 100, 1920) == 0, "the left edge normalizes to 0");
    Check(AxisToAbsCoord(100 + 1920, 100, 1920) == kAbsCoordMax, "the far edge saturates");
    Check(AxisToAbsCoord(50, 100, 1920) == 0, "left of the rectangle clamps to 0");
    Check(AxisToAbsCoord(500, 0, 0) == 0, "an empty extent normalizes to 0");

    const double mid = AbsCoordToAxis(kAbsCoordMax / 2, 0.0, 100.0);
    Check(mid > 49.9 && mid < 50.1, "the floating-point form agrees with the integer one");
}

void TestWheelNotches() {
    std::printf("[pointer] a wheel delta becomes whole notches, never zero...\n");
    Check(WheelNotches(0) == 0, "no movement is no notches");
    Check(WheelNotches(kWheelDeltaPerNotch) == 1, "one detent is one notch");
    Check(WheelNotches(-kWheelDeltaPerNotch) == -1, "and the other way round");
    Check(WheelNotches(3 * kWheelDeltaPerNotch) == 3, "three detents are three notches");
    Check(WheelNotches(40) == 1, "a partial detent still scrolls, rounded up in magnitude");
    Check(WheelNotches(-40) == -1, "including downward");
}

void TestTouchScrollAccumulates() {
    std::printf("[pointer] a touch drag turns into whole notches and keeps the remainder...\n");
    double carry = 0;

    Check(TakeScrollNotches(kTouchPointsPerNotch / 4, carry) == 0,
        "a short drag is not a notch yet");
    Check(TakeScrollNotches(kTouchPointsPerNotch / 4, carry) == 0, "nor is the next one");
    Check(TakeScrollNotches(kTouchPointsPerNotch / 2, carry) == 1,
        "but together they cross one notch");
    Check(carry < 1e-9 && carry > -1e-9, "and nothing is left over");

    Check(TakeScrollNotches(3 * kTouchPointsPerNotch, carry) == 3,
        "a long drag emits every notch it crossed, so a flick does not lose scroll");

    carry = 0;
    Check(TakeScrollNotches(-kTouchPointsPerNotch, carry) == -1, "dragging back scrolls back");

    carry = 0;
    Check(TakeScrollNotches(kTouchPointsPerNotch * 1.5, carry) == 1, "1.5 notches emits one");
    Check(TakeScrollNotches(kTouchPointsPerNotch * 0.5, carry) == 1,
        "and the half that was carried completes the next one");

    carry = 0;
    TakeScrollNotches(kTouchPointsPerNotch * 0.9, carry);
    Check(TakeScrollNotches(-kTouchPointsPerNotch * 1.8, carry) == 0,
        "reversing mid-drag cancels the pending notch instead of firing it");
}

struct FakeInjector : InputApplier<FakeInjector, uint16_t> {
    std::vector<std::string> calls;
    bool localActive = false;

    bool Apply(const InputEvent& e) {
        return DispatchInput(e, localActive);
    }

    void OnLocalUserTookOver() {
        calls.push_back("suppress");
    }
    void OnLocalUserIdle() {
        calls.push_back("resume");
    }
    void ReleaseAll() {
        calls.push_back("release");
    }
    void SendKey(int32_t vk, int32_t, bool down) {
        calls.push_back(std::string("key:") + std::to_string(vk) + (down ? ":down" : ":up"));
    }
    void SendButton(MouseButton, bool down) {
        calls.push_back(down ? "btn:down" : "btn:up");
    }
    void SendMoveAbsolute(int32_t, int32_t) {
        calls.push_back("move:abs");
    }
    void SendMoveRelative(int32_t, int32_t) {
        calls.push_back("move:rel");
    }
    void SendWheel(int32_t delta) {
        calls.push_back(std::string("wheel:") + std::to_string(delta));
    }
};

InputEvent Key(int32_t vk, bool down) {
    InputEvent e;
    e.type = InputType::Key;
    e.a = vk;
    e.b = 0x1E;
    e.state = down ? 1 : 0;
    return e;
}

InputEvent Move(bool absolute) {
    InputEvent e;
    e.type = InputType::MouseMove;
    e.absolute = absolute ? 1 : 0;
    return e;
}

InputEvent Wheel(int32_t delta) {
    InputEvent e;
    e.type = InputType::MouseWheel;
    e.b = delta;
    return e;
}

void TestDispatch() {
    std::printf("[applier] every event type reaches the matching backend call...\n");
    FakeInjector inj;

    Check(inj.Apply(Key('A', true)), "a key down is applied");
    Check(inj.Apply(Move(true)), "an absolute move is applied");
    Check(inj.Apply(Move(false)), "a relative move is applied");
    Check(inj.Apply(Wheel(240)), "a wheel event is applied");

    const std::vector<std::string> want{"key:65:down", "move:abs", "move:rel", "wheel:240"};
    Check(inj.calls == want, "the calls arrive in order with no extras");
    Check(inj.applied() == 4, "each applied event is counted");
    Check(inj.skipped() == 0, "and nothing was skipped");
}

void TestHostWinsReleasesHeldInput() {
    std::printf("[applier] the local user taking over releases what the remote still holds...\n");
    FakeInjector inj;
    Check(inj.Apply(Key('A', true)), "remote holds a key");

    inj.calls.clear();
    inj.localActive = true;
    Check(!inj.Apply(Key('B', true)), "the next remote event is refused");
    Check(inj.calls == std::vector<std::string>({"suppress", "release"}),
        "the backend is told once and asked to drop everything it holds");

    inj.calls.clear();
    Check(!inj.Apply(Key('C', true)), "still refused while the local user is active");
    Check(inj.calls.empty(), "and the transition is not reported twice");
    Check(inj.skipped() == 2, "both refused events are counted as skipped");

    inj.localActive = false;
    Check(inj.Apply(Key('D', true)), "input resumes once the local user goes idle");
    Check(inj.calls == std::vector<std::string>({"resume", "key:68:down"}),
        "the resume is reported once, then the event goes through");
}

}

void RunPointerMapTests() {
    TestClamp();
    TestPixelMapping();
    TestWheelNotches();
    TestTouchScrollAccumulates();
    TestDispatch();
    TestHostWinsReleasesHeldInput();
}
