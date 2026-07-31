#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>

#include "deskhub/input/PressedInputTracker.h"
#include "deskhub/protocol/Wire.h"

class LocalInputMonitor;

class InputInjector {
public:
    bool Init(HMONITOR monitor);

    void Apply(const deskhub::InputEvent& e);

    void ReleaseAll();

    void SetEnabled(bool on);
    bool enabled() const {
        return enabled_;
    }

    void SetLocalMonitor(LocalInputMonitor* mon) {
        localMon_ = mon;
    }

    uint64_t applied() const {
        return held_.applied();
    }
    uint64_t skipped() const {
        return held_.skipped();
    }

private:
    void SendKey(int32_t vk, int32_t scan, bool down);
    void SendButton(deskhub::MouseButton btn, bool down);
    void SendMoveAbsolute(int32_t nx, int32_t ny);
    void SendMoveRelative(int32_t dx, int32_t dy);

    HMONITOR monitor_ = nullptr;
    LocalInputMonitor* localMon_ = nullptr;
    bool enabled_ = true;

    deskhub::PressedInputTracker<int32_t> held_;
};
