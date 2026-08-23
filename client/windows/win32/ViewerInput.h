#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "deskhub/input/PointerLockState.h"
#include "deskhub/protocol/Wire.h"

struct DHScreen;

class ViewerInput {
public:
    bool Attach(HWND hwnd, DHScreen* session);
    void Detach();

    bool OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool relativeMode() const {
        return pointer_.locked();
    }
    void ToggleRelativeMode();

private:
    void ApplyLockEffect(const deskhub::PointerLockEffect& effect);
    void GrabPointer(bool locked);
    void OnRawInput(LPARAM lp);
    void EmitButton(deskhub::MouseButton button, bool down);

    HWND hwnd_ = nullptr;
    DHScreen* session_ = nullptr;
    deskhub::PointerLockState pointer_;
    bool attached_ = false;
    int buttonsDown_ = 0;
};
