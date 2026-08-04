#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ElevatedShare.h"

#include <shellapi.h>

#include "deskhub/session/ShareArgs.h"
#include "WinPaths.h"

#pragma comment(lib, "shell32.lib")

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD len = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                        sizeof(elevation), &len) != FALSE;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

bool RelaunchElevatedShare(std::span<const AgentSource> sources,
    const AgentOptions& opt, bool& outCancelled) {
    outCancelled = false;

    const std::wstring exe = SelfExePath();
    if (exe.empty()) return false;

    const std::wstring args = deskhub::BuildElevatedShareArgs(sources, opt);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) CloseHandle(sei.hProcess);
        return true;
    }
    outCancelled = GetLastError() == ERROR_CANCELLED;
    return false;
}
