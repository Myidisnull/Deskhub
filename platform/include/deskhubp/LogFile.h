#pragma once
#include <cstdio>
#include <ctime>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>

#include <chrono>
#include <thread>
#else
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#endif

namespace deskhubp {

inline std::string LogFileName() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
    const unsigned long pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    localtime_r(&now, &tm);
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    char name[80];
    std::snprintf(name, sizeof(name), "deskhub-%04d%02d%02d-%02d%02d%02d-%lu.log",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, pid);
    return std::string(name);
}

inline std::string LocalTimeHms() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
}

#ifdef _WIN32

inline std::wstring LogDirW() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();

    std::wstring dir = std::wstring(buf, n) + L"\\.deskhub";
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return std::wstring();
    return dir;
}

inline std::string LogDir() {
    const std::wstring w = LogDirW();
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

inline bool StartProcessLog(std::wstring* outPath = nullptr) {
    static char buffer[256 * 1024];

    const std::wstring dir = LogDirW();
    if (dir.empty()) return false;

    const std::string nameUtf8 = LogFileName();
    std::wstring name;
    name.reserve(nameUtf8.size());
    for (char c : nameUtf8) name.push_back(static_cast<wchar_t>(c));

    const std::wstring full = dir + L"\\" + name;

    std::FILE* redirected = nullptr;
    if (_wfreopen_s(&redirected, full.c_str(), L"w", stdout) != 0 || !redirected) return false;
    std::setvbuf(stdout, buffer, _IOFBF, sizeof(buffer));

    _dup2(_fileno(stdout), _fileno(stderr));
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    if (outPath) *outPath = full;

    SYSTEMTIME t{};
    GetLocalTime(&t);
    std::printf("[Deskhub] Log %ls started %04u-%02u-%02u %02u:%02u:%02u\n", full.c_str(),
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);

    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::fflush(stdout);
        }
    }).detach();

    return true;
}

#else

inline std::string LogDir() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        const struct passwd* pw = getpwuid(getuid());
        home = (pw && pw->pw_dir) ? pw->pw_dir : nullptr;
    }
    if (!home || !*home) return std::string();

    std::string dir = std::string(home) + "/.deskhub";
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return std::string();
    return dir;
}

inline std::string& LogPathRef() {
    static std::string path;
    return path;
}

inline std::FILE* LogHandle() {
    static std::FILE* file = []() -> std::FILE* {
        const std::string dir = LogDir();
        if (dir.empty()) return nullptr;

        const std::string path = dir + "/" + LogFileName();
        std::FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return nullptr;
        LogPathRef() = path;

        std::setvbuf(f, nullptr, _IOFBF, 256 * 1024);

        const std::string latest = dir + "/deskhub-latest.log";
        ::unlink(latest.c_str());
        ::symlink(path.c_str(), latest.c_str());

        std::thread([f] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::fflush(f);
            }
        }).detach();

        return f;
    }();
    return file;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline void LogEmit(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0) return;

    std::fprintf(stderr, "[Deskhub] %s\n", msg);
    if (std::FILE* f = LogHandle()) std::fprintf(f, "[Deskhub] %s\n", msg);
}

#endif

}
