#include "deskhubp/system/Autostart.h"

#include <windows.h>

#include <string>
#include <vector>

#include "deskhub/ui/AutostartConfig.h"
#include "deskhubp/diag/Log.h"

namespace deskhubp {
namespace {

std::wstring Widen(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0);
    std::wstring wide(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), wide.data(), n);
    return wide;
}

std::string SelfExePath() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, buf, int(n), nullptr, 0, nullptr, nullptr);
    std::string narrow(size_t(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, int(n), narrow.data(), bytes, nullptr, nullptr);
    return narrow;
}

bool RunSchtasks(const std::string& args, DWORD& exitCode) {
    std::wstring cmd = L"schtasks.exe " + Widen(args);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &si, &pi)) {
        LOGW("[Autostart] schtasks did not start: %lu", GetLastError());
        return false;
    }
    WaitForSingleObject(pi.hProcess, 15000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}

bool SetAutostartEnabled(bool on) {
    DWORD exitCode = 1;
    if (!on) {
        if (!RunSchtasks(deskhub::ui::BuildSchtasksDeleteArgs(), exitCode)) return false;
        return true;
    }
    const std::string exe = SelfExePath();
    if (exe.empty()) return false;
    if (!RunSchtasks(deskhub::ui::BuildSchtasksCreateArgs(exe), exitCode)) return false;
    if (exitCode != 0) LOGW("[Autostart] schtasks create exited with %lu", exitCode);
    return exitCode == 0;
}

bool AutostartEnabled() {
    DWORD exitCode = 1;
    if (!RunSchtasks(deskhub::ui::BuildSchtasksQueryArgs(), exitCode)) return false;
    return exitCode == 0;
}

}
