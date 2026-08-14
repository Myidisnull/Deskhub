#include "deskhubp/diag/LogFile.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "deskhub/ui/Brand.h"
#include "deskhub/ui/Strings.h"
#include "deskhub/ui/UiSettings.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/system/AppDataFile.h"

#ifdef _WIN32
#include <shellapi.h>
#include <share.h>
#else
#include <cerrno>
#include <cstdlib>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if __has_include(<TargetConditionals.h>)
#include <TargetConditionals.h>
#endif
#endif

namespace deskhubp {
namespace {

namespace fs = std::filesystem;

thread_local char g_brandMark[96];

const char* BrandMark() {
    std::snprintf(g_brandMark, sizeof(g_brandMark), "[%s]", deskhub::brand::kLogLineTag);
    return g_brandMark;
}

std::mutex& LogMutex() {
    static std::mutex mu;
    return mu;
}

deskhub::diag::LogPolicy& PolicyRef() {
    static deskhub::diag::LogPolicy policy;
    return policy;
}

std::string& ActiveLogPathRef() {
    static std::string path;
    return path;
}

std::FILE*& ActiveLogFileRef() {
    static std::FILE* file = nullptr;
    return file;
}

std::atomic<uint64_t>& BytesWrittenRef() {
    static std::atomic<uint64_t> bytes{0};
    return bytes;
}

std::atomic<bool>& FlushStartedRef() {
    static std::atomic<bool> started{false};
    return started;
}

fs::path PathFromUtf8(const std::string& utf8) {
    const std::u8string u8(utf8.begin(), utf8.end());
    return fs::path(u8);
}

std::string PathToUtf8(const fs::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

uint32_t Crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = uint32_t(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool WriteGzipStored(const fs::path& src, const fs::path& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ostringstream raw;
    raw << in.rdbuf();
    const std::string data = raw.str();
    const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
    const uint32_t crc = Crc32Update(0, bytes, data.size());
    const uint32_t isize = uint32_t(data.size());

    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    const uint8_t header[] = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff};
    out.write(reinterpret_cast<const char*>(header), sizeof(header));

    size_t offset = 0;
    while (offset < data.size()) {
        const size_t chunk = std::min(size_t(65535), data.size() - offset);
        const bool last = offset + chunk >= data.size();
        const uint8_t block = last ? 0x01 : 0x00;
        out.put(char(block));
        const uint16_t len = uint16_t(chunk);
        const uint16_t nlen = uint16_t(~len);
        out.put(char(len & 0xff));
        out.put(char((len >> 8) & 0xff));
        out.put(char(nlen & 0xff));
        out.put(char((nlen >> 8) & 0xff));
        out.write(data.data() + offset, std::streamsize(chunk));
        offset += chunk;
    }
    if (data.empty()) {
        const uint8_t emptyBlock[] = {0x01, 0x00, 0x00, 0xff, 0xff};
        out.write(reinterpret_cast<const char*>(emptyBlock), sizeof(emptyBlock));
    }

    const uint8_t trailer[] = {
        uint8_t(crc & 0xff),
        uint8_t((crc >> 8) & 0xff),
        uint8_t((crc >> 16) & 0xff),
        uint8_t((crc >> 24) & 0xff),
        uint8_t(isize & 0xff),
        uint8_t((isize >> 8) & 0xff),
        uint8_t((isize >> 16) & 0xff),
        uint8_t((isize >> 24) & 0xff),
    };
    out.write(reinterpret_cast<const char*>(trailer), sizeof(trailer));
    return bool(out);
}

void EnsureFlushThread() {
    bool expected = false;
    if (!FlushStartedRef().compare_exchange_strong(expected, true)) return;
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::lock_guard<std::mutex> lk(LogMutex());
            if (std::FILE* f = ActiveLogFileRef()) std::fflush(f);
        }
    }).detach();
}

std::string& LogDirOverrideStorage() {
    static std::string dir;
    return dir;
}

bool EnsureUsableDir(const std::string& dir, bool requireAbsolute) {
    if (dir.empty() || !deskhub::diag::IsPlausibleLogDir(dir)) return false;
    fs::path path = PathFromUtf8(dir);
    std::error_code ec;
    if (!path.is_absolute()) {
        if (requireAbsolute) return false;
        path = fs::absolute(path, ec);
        if (ec) return false;
    }
    if (fs::exists(path, ec)) {
        if (ec || !fs::is_directory(path, ec) || ec) return false;
    } else if (!fs::create_directories(path, ec) || ec) {
        return false;
    }

    const fs::path probe = path / (std::string(deskhub::brand::kDataDirName) + "-write-check");
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.put('x');
        if (!out) return false;
    }
    fs::remove(probe, ec);
    return true;
}

std::string ResolveDefaultConfigDir() {
    if (!AppDataDirRef().empty()) return AppDataDirRef();

#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::string();
    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, buf, int(n), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return std::string();
    std::string out(size_t(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, int(n), out.data(), bytes, nullptr, nullptr);
    out.push_back('\\');
    out += deskhub::brand::kDataDirName;
    return out;
#else
    const char* home = std::getenv("HOME");
    if (home && *home) return std::string(home) + "/" + deskhub::brand::kDataDirName;

    const struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir && *pw->pw_dir) return std::string(pw->pw_dir) + "/" + deskhub::brand::kDataDirName;

    const char* tmp = std::getenv("TMPDIR");
    if (tmp && *tmp) return std::string(tmp) + "/" + deskhub::brand::kDataDirName;
    return deskhub::brand::kDataDirName;
#endif
}

std::string ActiveLogDirLocked() {
    std::string dir = LogDirOverrideStorage();
    if (!dir.empty() && EnsureUsableDir(dir, true)) return dir;
    dir = ResolveDefaultConfigDir();
    if (!dir.empty() && EnsureUsableDir(dir, false)) {
        const fs::path path = PathFromUtf8(dir);
        std::error_code ec;
        if (path.is_absolute()) return dir;
        return PathToUtf8(fs::absolute(path, ec));
    }
    return std::string();
}

void WriteLogLineLocked(const char* text) {
    if (!ActiveLogFileRef() || !text || !*text) return;
    const size_t bytes = std::strlen(text);
    std::fwrite(text, 1, bytes, ActiveLogFileRef());
    BytesWrittenRef().fetch_add(uint64_t(bytes));
}

void CloseActiveLogLocked() {
    if (!ActiveLogFileRef()) return;
    std::fclose(ActiveLogFileRef());
    ActiveLogFileRef() = nullptr;
}

std::string DailyLogFileNameLocked() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char name[40];
    std::snprintf(name, sizeof(name), "%s-%04d%02d%02d.log", deskhub::brand::kLogFilePrefix, tm.tm_year + 1900, tm.tm_mon + 1,
        tm.tm_mday);
    return std::string(name);
}

std::string ArchiveLogFileNameLocked() {
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
    std::snprintf(name, sizeof(name), "%s-%04d%02d%02d-%02d%02d%02d-%lu.log", deskhub::brand::kLogFilePrefix,
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, pid);
    return std::string(name);
}

std::string HostNameUtf8() {
#ifdef _WIN32
    wchar_t buf[256] = {};
    DWORD n = 256;
    if (!GetComputerNameExW(ComputerNameDnsHostname, buf, &n) || n == 0) {
        n = 256;
        if (!GetComputerNameW(buf, &n) || n == 0) return "unknown";
    }
    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, buf, int(n), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return "unknown";
    std::string out(size_t(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, int(n), out.data(), bytes, nullptr, nullptr);
    return out;
#else
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf)) != 0) return "unknown";
    buf[sizeof(buf) - 1] = '\0';
    return std::string(buf);
#endif
}

const char* OsLabel() {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    return "iOS";
#else
    return "macOS";
#endif
#else
    return "Linux";
#endif
}

unsigned long CurrentPid() {
#ifdef _WIN32
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

void UpdateLatestSymlinkLocked(const fs::path& full) {
#if !defined(_WIN32)
    const fs::path latest = full.parent_path() / (std::string(deskhub::brand::kLogFilePrefix) + "-latest.log");
    std::error_code ec;
    fs::remove(latest, ec);
    fs::create_symlink(full, latest, ec);
#else
    (void)full;
#endif
}

bool OpenPathLocked(const std::string& path, bool append) {
    CloseActiveLogLocked();

#ifdef _WIN32
    const std::wstring wpath = WidenUtf8(path);
    std::FILE* f = _wfsopen(wpath.c_str(), append ? L"a" : L"w", _SH_DENYWR);
    if (!f) return false;
#else
    std::FILE* f = std::fopen(path.c_str(), append ? "a" : "w");
    if (!f) return false;
#endif

    std::setvbuf(f, nullptr, _IOFBF, std::size_t{256} * 1024);
    ActiveLogFileRef() = f;
    ActiveLogPathRef() = path;

    uint64_t bytes = 0;
    if (append) {
        std::error_code ec;
        const auto size = fs::file_size(PathFromUtf8(path), ec);
        if (!ec) bytes = uint64_t(size);
    }
    BytesWrittenRef().store(bytes);
    UpdateLatestSymlinkLocked(PathFromUtf8(path));
    EnsureFlushThread();
    return true;
}

bool ArchivePathLocked(const std::string& path) {
    if (path.empty()) return false;
    const fs::path src = PathFromUtf8(path);
    std::error_code ec;
    if (!fs::exists(src, ec) || ec) return false;

    for (int attempt = 0; attempt < 32; ++attempt) {
        std::string name = ArchiveLogFileNameLocked();
        if (attempt > 0) {
            const size_t dot = name.rfind('.');
            if (dot == std::string::npos) return false;
            name.insert(dot, "-" + std::to_string(attempt));
        }
        const fs::path dst = src.parent_path() / PathFromUtf8(name);
        if (fs::exists(dst, ec) && !ec) continue;
        fs::rename(src, dst, ec);
        return !ec;
    }
    return false;
}

bool OpenOrAppendLogFileLocked() {
    const std::string dir = ActiveLogDirLocked();
    if (dir.empty()) return false;

    const fs::path full = PathFromUtf8(dir) / PathFromUtf8(DailyLogFileNameLocked());
    const std::string path = PathToUtf8(full);
    std::error_code ec;
    if (fs::exists(full, ec) && !ec) {
        const auto size = fs::file_size(full, ec);
        if (!ec && uint64_t(size) >= deskhub::diag::LogMaxFileBytes(PolicyRef())) {
            if (!ArchivePathLocked(path)) return false;
            return OpenPathLocked(path, false);
        }
        return OpenPathLocked(path, true);
    }
    return OpenPathLocked(path, false);
}

bool RotateToFreshLogFileLocked() {
    const std::string previous = ActiveLogPathRef();
    CloseActiveLogLocked();
    if (!previous.empty()) {
        if (!ArchivePathLocked(previous)) {
            return OpenPathLocked(previous, true);
        }
    }

    const std::string dir = ActiveLogDirLocked();
    if (dir.empty()) return false;
    const fs::path full = PathFromUtf8(dir) / PathFromUtf8(DailyLogFileNameLocked());
    return OpenPathLocked(PathToUtf8(full), false);
}

bool EnsureDailyFileLocked() {
    if (!ActiveLogFileRef()) return OpenOrAppendLogFileLocked();

    const std::string expected = DailyLogFileNameLocked();
    const fs::path active = PathFromUtf8(ActiveLogPathRef());
    const std::string activeName = PathToUtf8(active.filename());
    if (activeName == expected) return true;

    if (!RotateToFreshLogFileLocked()) return false;

    char line[160];
    const int n = std::snprintf(line, sizeof(line),
        "%s %s Log rolled to new day -> %s\n", BrandMark(), LocalTimeHms().c_str(),
        ActiveLogPathRef().c_str());
    if (n > 0 && ActiveLogFileRef()) {
        std::fwrite(line, 1, size_t(n), ActiveLogFileRef());
        BytesWrittenRef().fetch_add(uint64_t(n));
    }
    return true;
}

void WriteProcessBannerLocked() {
    if (!ActiveLogFileRef()) return;

    char line[768];
    int n = std::snprintf(line, sizeof(line),
        "%s -------- process start %s --------\n", BrandMark(), LocalTimeHms().c_str());
    if (n > 0) WriteLogLineLocked(line);

    n = std::snprintf(line, sizeof(line), "%s log=%s\n", BrandMark(), ActiveLogPathRef().c_str());
    if (n > 0) WriteLogLineLocked(line);

    n = std::snprintf(line, sizeof(line), "%s %s\n", BrandMark(), deskhub::ui::VersionLine().c_str());
    if (n > 0) WriteLogLineLocked(line);

    n = std::snprintf(line, sizeof(line), "%s host=%s os=%s pid=%lu\n", BrandMark(),
        HostNameUtf8().c_str(), OsLabel(), CurrentPid());
    if (n > 0) WriteLogLineLocked(line);

    {
        std::string ips;
        for (const AdapterAddr& a : ListLocalIPv4()) {
            if (a.ip.empty()) continue;
            if (!ips.empty()) ips += ", ";
            ips += a.ip;
            if (!a.name.empty()) {
                ips += " (";
                ips += a.name;
                ips += ")";
            }
        }
        if (ips.empty()) ips = "(none)";
        n = std::snprintf(line, sizeof(line), "%s ipv4: %s\n", BrandMark(), ips.c_str());
        if (n > 0) WriteLogLineLocked(line);
    }

    const deskhub::ui::UiSettings settings =
        deskhub::ui::ParseUiSettings(ReadAppDataFile("ui-settings.txt"));
    n = std::snprintf(line, sizeof(line),
        "%s settings: fps=%u bitrateMbps=%u maxDim=%u port=%u allowInput=%s "
        "clientControl=%s background=%s hideTray=%s autoShare=%s passcode=%s\n",
        BrandMark(), settings.fps, settings.bitrateMbps, settings.maxDim, settings.port,
        settings.allowInput ? "on" : "off", settings.clientControl ? "on" : "off",
        settings.runInBackground ? "on" : "off", settings.hideTrayIcon ? "on" : "off",
        settings.autoShare ? "on" : "off", settings.passcode.empty() ? "unset" : "set");
    if (n > 0) WriteLogLineLocked(line);

    const deskhub::diag::LogPolicy policy = PolicyRef();
    const std::string logDir = ActiveLogDirLocked();
    n = std::snprintf(line, sizeof(line),
        "%s logDir=%s retention: split>%uMB compress>%udays delete>%udays\n", BrandMark(),
        logDir.c_str(), policy.maxFileMb, policy.compressAfterDays, policy.deleteAfterDays);
    if (n > 0) WriteLogLineLocked(line);

    {
        char banner[160];
        const int bn = std::snprintf(banner, sizeof(banner),
            "%s Session diagnostics are written while Share or Connect is active.\n", BrandMark());
        if (bn > 0) WriteLogLineLocked(banner);
    }
    std::fflush(ActiveLogFileRef());
}

void RotateIfNeededLocked() {
    if (!EnsureDailyFileLocked()) return;

    const uint64_t limit = deskhub::diag::LogMaxFileBytes(PolicyRef());
    if (BytesWrittenRef().load() < limit) return;
    if (!RotateToFreshLogFileLocked()) return;

    char line[160];
    const int n = std::snprintf(line, sizeof(line),
        "%s %s Log rotated -> %s\n", BrandMark(), LocalTimeHms().c_str(), ActiveLogPathRef().c_str());
    if (n > 0 && ActiveLogFileRef()) {
        std::fwrite(line, 1, size_t(n), ActiveLogFileRef());
        BytesWrittenRef().fetch_add(uint64_t(n));
    }
}

double FileAgeDays(const fs::path& path) {
    std::error_code ec;
    const auto ftime = fs::last_write_time(path, ec);
    if (ec) return 0;
    const auto age = fs::file_time_type::clock::now() - ftime;
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(age).count();
    if (secs <= 0) return 0;
    return double(secs) / 86400.0;
}

}

std::string& AppDataDirRef() {
    static std::string dir;
    return dir;
}

void SetAppDataDir(std::string dir) {
    AppDataDirRef() = std::move(dir);
}

std::string ConfigDir() {
    const std::string dir = ResolveDefaultConfigDir();
    if (dir.empty() || !EnsureUsableDir(dir, false)) return std::string();
    const fs::path path = PathFromUtf8(dir);
    std::error_code ec;
    if (path.is_absolute()) return dir;
    return PathToUtf8(fs::absolute(path, ec));
}

std::string LogDirOverride() {
    std::lock_guard<std::mutex> lk(LogMutex());
    return LogDirOverrideStorage();
}

bool IsUsableLogDir(const std::string& dir) {
    if (dir.empty()) return true;
    return EnsureUsableDir(dir, true);
}

bool SetLogDirOverride(std::string dir) {
    if (!dir.empty() && !EnsureUsableDir(dir, true)) return false;
    std::lock_guard<std::mutex> lk(LogMutex());
    if (dir == LogDirOverrideStorage()) return true;
    LogDirOverrideStorage() = std::move(dir);
    if (ActiveLogFileRef()) {
        if (!OpenOrAppendLogFileLocked()) return false;
        WriteProcessBannerLocked();
    }
    return true;
}

std::string LogFileName() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char name[40];
    std::snprintf(name, sizeof(name), "%s-%04d%02d%02d.log", deskhub::brand::kLogFilePrefix, tm.tm_year + 1900, tm.tm_mon + 1,
        tm.tm_mday);
    return std::string(name);
}

std::string LocalTimeHms() {
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

std::string LogDir() {
    std::lock_guard<std::mutex> lk(LogMutex());
    return ActiveLogDirLocked();
}

bool StartProcessLog() {
    std::lock_guard<std::mutex> lk(LogMutex());
    if (!EnsureDailyFileLocked()) return false;
    WriteProcessBannerLocked();
    return true;
}

#ifdef _WIN32

std::wstring WidenUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), out.data(), n);
    return out;
}

std::wstring LogDirW() {
    return WidenUtf8(LogDir());
}

#endif

void SetLogPolicy(deskhub::diag::LogPolicy policy) {
    std::lock_guard<std::mutex> lk(LogMutex());
    PolicyRef() = deskhub::diag::ClampLogPolicy(policy);
}

deskhub::diag::LogPolicy GetLogPolicy() {
    std::lock_guard<std::mutex> lk(LogMutex());
    return PolicyRef();
}

void LogEmit(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0) return;

#if !defined(_WIN32) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    std::fprintf(stderr, "%s %s\n", BrandMark(), msg);
#elif defined(_WIN32)
    std::fprintf(stderr, "%s %s\n", BrandMark(), msg);
#endif

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    (void)msg;
    return;
#else
    std::lock_guard<std::mutex> lk(LogMutex());
    if (!ActiveLogFileRef()) {
        if (!OpenOrAppendLogFileLocked()) return;
        WriteProcessBannerLocked();
    }
    RotateIfNeededLocked();
    if (!ActiveLogFileRef()) return;

    char line[1100];
    const int written =
        std::snprintf(line, sizeof(line), "%s %s %s\n", BrandMark(), LocalTimeHms().c_str(), msg);
    if (written <= 0) return;
    const size_t bytes = size_t(written);
    std::fwrite(line, 1, bytes, ActiveLogFileRef());
    BytesWrittenRef().fetch_add(uint64_t(bytes));
    RotateIfNeededLocked();
#endif
}

std::string CurrentLogPath() {
    std::lock_guard<std::mutex> lk(LogMutex());
    return ActiveLogPathRef();
}

std::vector<LogFileInfo> ListLogFiles() {
    std::vector<LogFileInfo> out;
    const std::string dir = LogDir();
    if (dir.empty()) return out;

    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(PathFromUtf8(dir), ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = PathToUtf8(entry.path().filename());
        if (!deskhub::diag::IsDeskhubLogName(name) && !deskhub::diag::IsDeskhubGzipLogName(name))
            continue;
        LogFileInfo info;
        info.name = name;
        info.path = PathToUtf8(entry.path());
        info.sizeBytes = uint64_t(entry.file_size(ec));
        if (ec) info.sizeBytes = 0;
        out.push_back(std::move(info));
    }

    std::sort(out.begin(), out.end(),
        [](const LogFileInfo& a, const LogFileInfo& b) { return a.name > b.name; });
    return out;
}

std::string ReadLogFile(const std::string& path, size_t maxBytes) {
    if (path.empty() || maxBytes == 0) return {};
    const fs::path file = PathFromUtf8(path);
    const std::string name = PathToUtf8(file.filename());
    if (!deskhub::diag::IsDeskhubLogName(name)) return {};

    {
        std::lock_guard<std::mutex> lk(LogMutex());
        if (ActiveLogFileRef() && ActiveLogPathRef() == path) std::fflush(ActiveLogFileRef());
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end <= 0) return {};
    const size_t size = size_t(end);
    const size_t start = size > maxBytes ? size - maxBytes : 0;
    in.seekg(std::streamoff(start), std::ios::beg);
    std::string out(size - start, '\0');
    in.read(out.data(), std::streamsize(out.size()));
    out.resize(size_t(in.gcount()));
    if (start > 0) out.insert(0, "...\n");
    return out;
}

void ApplyLogMaintenance() {
    const deskhub::diag::LogPolicy policy = GetLogPolicy();
    const std::string dir = LogDir();
    if (dir.empty()) return;

    const std::string current = CurrentLogPath();
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(PathFromUtf8(dir), ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const fs::path path = entry.path();
        const std::string name = PathToUtf8(path.filename());
        const std::string full = PathToUtf8(path);
        if (full == current) continue;

        const double age = FileAgeDays(path);
        if (policy.deleteAfterDays > 0 && age >= double(policy.deleteAfterDays)) {
            if (deskhub::diag::IsDeskhubLogName(name) || deskhub::diag::IsDeskhubGzipLogName(name))
                fs::remove(path, ec);
            continue;
        }

        if (policy.compressAfterDays > 0 && age >= double(policy.compressAfterDays) &&
            deskhub::diag::IsDeskhubLogName(name)) {
            fs::path gzPath = path;
            gzPath += ".gz";
            if (fs::exists(gzPath, ec)) {
                fs::remove(path, ec);
                continue;
            }
            if (WriteGzipStored(path, gzPath)) fs::remove(path, ec);
        }
    }
}

bool OpenLogFolder() {
    const std::string dir = LogDir();
    if (dir.empty()) return false;
#ifdef _WIN32
    const std::wstring wdir = WidenUtf8(dir);
    const HINSTANCE rc =
        ShellExecuteW(nullptr, L"open", wdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(rc) > 32;
#else
    const std::string cmd = "xdg-open \"" + dir + "\" >/dev/null 2>&1 &";
#if defined(__APPLE__)
    const std::string openCmd = "open \"" + dir + "\" >/dev/null 2>&1 &";
    return std::system(openCmd.c_str()) == 0;
#else
    return std::system(cmd.c_str()) == 0;
#endif
#endif
}

}
