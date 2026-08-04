#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>

inline std::wstring SelfExePath() {
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return (n == 0 || n >= MAX_PATH) ? std::wstring() : std::wstring(path, n);
}
