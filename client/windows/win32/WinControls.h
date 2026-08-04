#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

class ChildControlFactory {
public:
    ChildControlFactory(HWND parent, HINSTANCE instance)
        : parent_(parent), instance_(instance),
          font_((HFONT)GetStockObject(DEFAULT_GUI_FONT)) {}

    HWND operator()(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w,
        int h, int id) const {
        HWND c = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, x, y, w, h, parent_,
            (HMENU)(INT_PTR)id, instance_, nullptr);
        if (c) SendMessageW(c, WM_SETFONT, (WPARAM)font_, TRUE);
        return c;
    }

private:
    HWND parent_;
    HINSTANCE instance_;
    HFONT font_;
};

template <class KeepGoing>
bool PumpMessagesUntil(HWND dialogOwner, KeepGoing keepGoing) {
    MSG msg;
    BOOL got = TRUE;
    while (keepGoing() && (got = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (got == -1) break;
        if (!dialogOwner || !IsDialogMessageW(dialogOwner, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return got == 0;
}
