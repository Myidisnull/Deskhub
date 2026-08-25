#pragma once
#include "deskhub/diag/LogPolicy.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace deskhubp {

std::string& AppDataDirRef();
void SetAppDataDir(std::string dir);

std::string ConfigDir();
std::string LogFileName();
std::string LocalTimeHms();
std::string LogDir();
std::string LogDirOverride();
bool IsUsableLogDir(const std::string& dir);
bool SetLogDirOverride(std::string dir);
bool StartProcessLog();

#ifdef _WIN32
std::wstring WidenUtf8(const std::string& s);
std::wstring LogDirW();
#endif

void SetLogPolicy(deskhub::diag::LogPolicy policy);
deskhub::diag::LogPolicy GetLogPolicy();

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void LogEmit(const char* fmt, ...);

struct LogFileInfo {
    std::string name;
    std::string path;
    uint64_t sizeBytes = 0;
};

std::string CurrentLogPath();
std::vector<LogFileInfo> ListLogFiles();
std::string ReadLogFile(const std::string& path, size_t maxBytes);
void ApplyLogMaintenance();
bool OpenLogFolder();

}
