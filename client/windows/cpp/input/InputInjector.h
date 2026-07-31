#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <map>
#include <set>

#include "deskhub/protocol/Wire.h"

class InputInjector {
public:
    bool Init(HMONITOR monitor);

    void Apply(const deskhub::InputEvent& e);

    void ReleaseAll();

    void SetEnabled(bool on);
    bool enabled() const {
        return enabled_;
    }

    uint64_t applied() const {
        return applied_;
    }
    uint64_t skipped() const {
        return skipped_;
    }

private:
    void SendKey(int32_t vk, int32_t scan, bool down);
    void SendButton(deskhub::MouseButton btn, bool down);
    void SendMoveAbsolute(int32_t nx, int32_t ny);
    void SendMoveRelative(int32_t dx, int32_t dy);

    HMONITOR monitor_ = nullptr;
    bool enabled_ = true;
    bool localSuppressed_ = false;
    uint64_t applied_ = 0;
    uint64_t skipped_ = 0;
    std::map<int32_t, int32_t> keysDown_;
    std::set<deskhub::MouseButton> buttonsDown_;
};
