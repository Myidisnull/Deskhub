#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "DiagLog.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <io.h>
#include <thread>

#include "deskhubp/LogFile.h"

namespace {

char g_logBuf[256 * 1024];

}

bool StartProcessLog(std::wstring* outPath) {
    SYSTEMTIME t{};
    GetLocalTime(&t);

    const std::string nameUtf8 = deskhubp::LogFileName();
    std::wstring name;
    name.reserve(nameUtf8.size());
    for (char c : nameUtf8) name.push_back(static_cast<wchar_t>(c));

    const std::wstring dir = deskhubp::LogDirW();
    if (dir.empty()) return false;
    const std::wstring full = dir + L"\\" + name;

    if (!_wfreopen(full.c_str(), L"w", stdout)) return false;
    setvbuf(stdout, g_logBuf, _IOFBF, sizeof(g_logBuf));

    _dup2(_fileno(stdout), _fileno(stderr));
    setvbuf(stderr, nullptr, _IONBF, 0);

    if (outPath) *outPath = full;

    std::printf("[DiagLog] %ls started %04u-%02u-%02u %02u:%02u:%02u\n",
        full.c_str(), t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);

    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::fflush(stdout);
        }
    }).detach();

    return true;
}
