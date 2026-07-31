#pragma once
#include <cstdint>
#include <map>
#include <set>

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
        return applied_;
    }
    uint64_t skipped() const {
        return skipped_;
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
    bool localSuppressed_ = false;
    uint64_t applied_ = 0;
    uint64_t skipped_ = 0;

    double rectX_ = 0, rectY_ = 0, rectW_ = 0, rectH_ = 0;
    uint64_t rectUs_ = 0;

    std::map<int32_t, uint16_t> keysDown_;
    std::set<deskhub::MouseButton> buttonsDown_;
    std::set<int32_t> modsDown_;

    uint64_t lastClickUs_ = 0;
    double lastClickX_ = 0, lastClickY_ = 0;
    int64_t clickState_ = 1;
    deskhub::MouseButton lastClickBtn_ = deskhub::MouseButton::Left;
};
