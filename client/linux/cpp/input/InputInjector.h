#pragma once
#include <cstdint>
#include <map>
#include <set>

#include "deskhub/protocol/Wire.h"

class LocalInputMonitor;

class InputInjector {
public:
    static constexpr const char* kKeyboardName = "Deskhub Keyboard";
    static constexpr const char* kPointerName = "Deskhub Mouse";
    static constexpr const char* kAbsPointerName = "Deskhub Absolute Mouse";

    InputInjector();
    ~InputInjector();
    InputInjector(const InputInjector&) = delete;
    InputInjector& operator=(const InputInjector&) = delete;

    bool Init(int32_t srcX, int32_t srcY, uint32_t srcW, uint32_t srcH, int32_t deskX,
        int32_t deskY, uint32_t deskW, uint32_t deskH);

    void Apply(const deskhub::InputEvent& e);

    void ReleaseAll();

    void SetEnabled(bool on) {
        enabled_ = on;
    }
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
    void SendKey(int32_t vk, bool down);
    void SendButton(deskhub::MouseButton btn, bool down);
    void SendMoveAbsolute(int32_t nx, int32_t ny);
    void SendMoveRelative(int32_t dx, int32_t dy);
    void SendWheel(int32_t delta);

    int kbdFd_ = -1;
    int mouseFd_ = -1;
    int absFd_ = -1;

    int32_t srcX_ = 0, srcY_ = 0;
    uint32_t srcW_ = 0, srcH_ = 0;
    int32_t deskX_ = 0, deskY_ = 0;
    uint32_t deskW_ = 0, deskH_ = 0;

    LocalInputMonitor* localMon_ = nullptr;
    bool enabled_ = true;
    bool localSuppressed_ = false;
    uint64_t applied_ = 0;
    uint64_t skipped_ = 0;

    std::map<int32_t, uint16_t> keysDown_;
    std::set<deskhub::MouseButton> buttonsDown_;
};
