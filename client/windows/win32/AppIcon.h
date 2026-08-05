#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

inline HICON LoadAppIcon(int size) {
    return (HICON)LoadImageW(GetModuleHandleW(nullptr), L"deskhub_app_icon", IMAGE_ICON, size,
        size, LR_SHARED);
}

inline void SetAppWindowIcon(HWND hwnd) {
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadAppIcon(GetSystemMetrics(SM_CXICON)));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadAppIcon(GetSystemMetrics(SM_CXSMICON)));
}
