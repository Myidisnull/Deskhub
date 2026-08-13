#include "deskhubp/system/Autostart.h"

#include <sys/stat.h>
#include <unistd.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "deskhub/ui/AutostartConfig.h"
#include "deskhubp/diag/Log.h"

namespace deskhubp {
namespace {

std::string AutostartDir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/autostart";
    const char* home = std::getenv("HOME");
    if (home && *home) return std::string(home) + "/.config/autostart";
    return {};
}

std::string AutostartFilePath() {
    const std::string dir = AutostartDir();
    if (dir.empty()) return {};
    return dir + "/" + deskhub::ui::kAutostartDesktopFileName;
}

std::string SelfExePath() {
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return buf;
}

bool EnsureDirs(const std::string& dir) {
    size_t pos = 0;
    while (pos < dir.size()) {
        const size_t next = dir.find('/', pos + 1);
        const std::string partial =
            dir.substr(0, next == std::string::npos ? dir.size() : next);
        if (!partial.empty()) mkdir(partial.c_str(), 0755);
        if (next == std::string::npos) break;
        pos = next;
    }
    struct stat st{};
    return stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

}

bool SetAutostartEnabled(bool on) {
    const std::string path = AutostartFilePath();
    if (path.empty()) return false;

    if (!on) {
        if (unlink(path.c_str()) != 0 && errno != ENOENT) {
            LOGW("[Autostart] could not remove %s: %d", path.c_str(), errno);
            return false;
        }
        return true;
    }

    const std::string exe = SelfExePath();
    if (exe.empty()) return false;
    if (!EnsureDirs(AutostartDir())) return false;

    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        LOGW("[Autostart] could not write %s: %d", path.c_str(), errno);
        return false;
    }
    const std::string entry = deskhub::ui::BuildXdgAutostartEntry(exe);
    const bool ok = std::fwrite(entry.data(), 1, entry.size(), f) == entry.size();
    std::fclose(f);
    return ok;
}

bool AutostartEnabled() {
    const std::string path = AutostartFilePath();
    return !path.empty() && access(path.c_str(), F_OK) == 0;
}

}
