#include "deskhubp/input/LocalInput.h"

#include <linux/input.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "deskhub/ui/Brand.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/diag/Log.h"

namespace {

constexpr uint64_t kRescanUs = 5'000'000;
constexpr const char* kInputDir = "/dev/input";

bool ShouldWatch(int fd) {
    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) return false;
    if (std::strncmp(name, deskhub::brand::kProductName, std::strlen(deskhub::brand::kProductName)) ==
        0)
        return false;

    unsigned long evBits = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), &evBits) < 0) return false;
    const bool hasKey = evBits & (1UL << EV_KEY);
    const bool hasRel = evBits & (1UL << EV_REL);
    return hasKey || hasRel;
}

}

struct LocalInputMonitor::Impl {
    std::thread thread;
    std::atomic<bool> quit{false};
    std::atomic<uint64_t> lastUs{0};
    std::vector<int> fds;
    bool warnedNoAccess = false;

    void CloseAll() {
        for (int fd : fds)
            if (fd >= 0) close(fd);
        fds.clear();
    }

    void Rescan() {
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
            fds.push_back(fd);
        }
        closedir(dir);

        if (fds.empty() && !warnedNoAccess) {
            warnedNoAccess = true;
            LOGW(
                "[HostWins] Cannot read /dev/input/event* — the person at this machine will NOT "
                "get priority over the remote user. Add your user to the 'input' group to enable "
                "it.");
        }
    }

    void Run() {
        Rescan();
        uint64_t lastScanUs = NowUs();

        std::vector<pollfd> pfds;
        input_event evs[32];

        while (!quit.load(std::memory_order_relaxed)) {
            const uint64_t now = NowUs();
            if (now - lastScanUs >= kRescanUs) {
                lastScanUs = now;
                Rescan();
            }

            if (fds.empty()) {
                poll(nullptr, 0, 200);
                continue;
            }

            pfds.clear();
            pfds.reserve(fds.size());
            for (int fd : fds) pfds.push_back(pollfd{fd, POLLIN, 0});

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
            if (sawInput) lastUs.store(NowUs(), std::memory_order_relaxed);
        }
    }
};

LocalInputMonitor::LocalInputMonitor()
    : impl_(std::make_unique<Impl>()) {}

LocalInputMonitor::~LocalInputMonitor() {
    Stop();
}

void LocalInputMonitor::Start() {
    if (impl_->thread.joinable()) return;
    impl_->quit.store(false);
    impl_->thread = std::thread([im = impl_.get()] { im->Run(); });
}

void LocalInputMonitor::Stop() {
    impl_->quit.store(true);
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->CloseAll();
}

uint64_t LocalInputMonitor::lastLocalUs() const {
    return impl_->lastUs.load(std::memory_order_relaxed);
}
