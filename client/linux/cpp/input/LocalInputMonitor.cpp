#include "input/LocalInputMonitor.h"

#include <linux/input.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>

#include "deskhubp/Log.h"
#include "deskhubp/Clock.h"
#include "input/InputInjector.h"

namespace {

constexpr uint64_t kRescanUs = 5'000'000;
constexpr const char* kInputDir = "/dev/input";

bool ShouldWatch(int fd) {
    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) return false;
    if (std::strncmp(name, "Deskhub", 7) == 0) return false;

    unsigned long evBits = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), &evBits) < 0) return false;
    const bool hasKey = evBits & (1UL << EV_KEY);
    const bool hasRel = evBits & (1UL << EV_REL);
    return hasKey || hasRel;
}

}

LocalInputMonitor::~LocalInputMonitor() {
    Stop();
}

void LocalInputMonitor::Start() {
    if (thread_.joinable()) return;
    quit_.store(false);
    thread_ = std::thread([this] { Run(); });
}

void LocalInputMonitor::Stop() {
    quit_.store(true);
    if (thread_.joinable()) thread_.join();
    CloseAll();
}

void LocalInputMonitor::CloseAll() {
    for (int fd : fds_)
        if (fd >= 0) close(fd);
    fds_.clear();
}

void LocalInputMonitor::Rescan() {
    CloseAll();

    DIR* dir = opendir(kInputDir);
    if (!dir) return;
    while (dirent* ent = readdir(dir)) {
        if (std::strncmp(ent->d_name, "event", 5) != 0) continue;
        char path[512];
        std::snprintf(path, sizeof(path), "%s/%s", kInputDir, ent->d_name);
        const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (!ShouldWatch(fd)) {
            close(fd);
            continue;
        }
        fds_.push_back(fd);
    }
    closedir(dir);

    if (fds_.empty() && !warnedNoAccess_) {
        warnedNoAccess_ = true;
        LOGW(
            "[HostWins] Cannot read /dev/input/event* — the person at this machine will NOT get "
            "priority over the remote user. Add your user to the 'input' group to enable it.");
    }
}

void LocalInputMonitor::Run() {
    Rescan();
    uint64_t lastScanUs = NowUs();

    std::vector<pollfd> pfds;
    input_event evs[32];

    while (!quit_.load(std::memory_order_relaxed)) {
        const uint64_t now = NowUs();
        if (now - lastScanUs >= kRescanUs) {
            lastScanUs = now;
            Rescan();
        }

        if (fds_.empty()) {
            poll(nullptr, 0, 200);
            continue;
        }

        pfds.clear();
        pfds.reserve(fds_.size());
        for (int fd : fds_) pfds.push_back(pollfd{fd, POLLIN, 0});

        const int n = poll(pfds.data(), pfds.size(), 100);
        if (n <= 0) continue;

        bool sawInput = false;
        for (size_t i = 0; i < pfds.size(); ++i) {
            if (!(pfds[i].revents & POLLIN)) {
                if (pfds[i].revents) lastScanUs = 0;
                continue;
            }
            const ssize_t rd = read(pfds[i].fd, evs, sizeof(evs));
            if (rd < ssize_t(sizeof(input_event))) continue;
            const size_t count = size_t(rd) / sizeof(input_event);
            for (size_t k = 0; k < count; ++k) {
                if (evs[k].type == EV_KEY || evs[k].type == EV_REL || evs[k].type == EV_ABS)
                    sawInput = true;
            }
        }
        if (sawInput) lastUs_.store(NowUs(), std::memory_order_relaxed);
    }
}
