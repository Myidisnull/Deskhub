#pragma once
#include <cstdint>

#include "deskhub/input/PressedInputTracker.h"
#include "deskhub/protocol/Wire.h"

class LocalInputMonitor;

class InputInjector {
public:
    InputInjector();
    ~InputInjector();
    InputInjector(const InputInjector&) = delete;
    InputInjector& operator=(const InputInjector&) = delete;

    bool Init(uint32_t displayId);

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
    bool SourceRect(double& x, double& y, double& w, double& h);
    void SendKey(int32_t vk, bool down);
    void SendButton(deskhub::MouseButton btn, bool down);
    void SendMoveAbsolute(int32_t nx, int32_t ny);
    void SendMoveRelative(int32_t dx, int32_t dy);
    void SendWheel(int32_t delta);
    void PostMouseAt(double x, double y, int32_t dx, int32_t dy);
    uint64_t CurrentFlags() const;

    uint32_t displayId_ = 0;
    void* source_ = nullptr;
    LocalInputMonitor* localMon_ = nullptr;

    bool enabled_ = true;

    double rectX_ = 0, rectY_ = 0, rectW_ = 0, rectH_ = 0;
    uint64_t rectUs_ = 0;

    deskhub::PressedInputTracker<uint16_t> held_;

    uint64_t lastClickUs_ = 0;
    double lastClickX_ = 0, lastClickY_ = 0;
    int64_t clickState_ = 1;
    deskhub::MouseButton lastClickBtn_ = deskhub::MouseButton::Left;
};
