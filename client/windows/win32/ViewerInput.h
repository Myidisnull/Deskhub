#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "deskhub/protocol/Wire.h"

struct DHSession;

class ViewerInput {
public:
    bool Attach(HWND hwnd, DHSession* session);
    void Detach();

    bool OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool relativeMode() const {
        return relative_;
    }
    void ToggleRelativeMode();

private:
    void SetRelativeMode(bool on);
    void OnRawInput(LPARAM lp);
    void EmitButton(deskhub::MouseButton button, bool down);

    HWND hwnd_ = nullptr;
    DHSession* session_ = nullptr;
    bool relative_ = false;
    bool attached_ = false;
    int buttonsDown_ = 0;
};
