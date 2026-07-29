// =============================================================================
// LocalInputMonitor.cpp — đọc /dev/input/event* trên một thread nền.
//
// VÒNG LẶP LÀM ĐÚNG HAI VIỆC
//   1. poll() mọi fd với hạn 100 ms — đủ ngắn để Stop() không phải chờ lâu.
//   2. Mỗi 5 giây quét lại danh sách thiết bị: cắm thêm bàn phím USB giữa phiên
//      là chuyện thường, và thiết bị ảo của chính ta cũng có thể xuất hiện sau
//      lúc Start().
//
// LIÊN QUAN: input/LocalInputMonitor.h (⚠ vì sao phải lọc thiết bị của chính mình)
// =============================================================================
#include "input/LocalInputMonitor.h"

#include <linux/input.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>

#include "Log.h"
#include "deskhubp/Clock.h"
#include "input/InputInjector.h"

namespace {

constexpr uint64_t kRescanUs = 5'000'000;
constexpr const char* kInputDir = "/dev/input";

// Thiết bị này có phải bàn phím/chuột thật không, và có phải của CHÍNH TA không?
// Trả về true nếu nên theo dõi.
bool ShouldWatch(int fd) {
    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) return false;
    // Lọc thiết bị ảo của InputInjector — xem ⚠ ở LocalInputMonitor.h.
    if (std::strncmp(name, "Deskhub", 7) == 0) return false;

    // Chỉ quan tâm thiết bị phát EV_KEY (bàn phím, nút chuột) hoặc EV_REL (chuột).
    // Bỏ qua cảm biến, nút nguồn ảo, loa... — chúng không phải "người dùng đang
    // gõ" và một số phát sự kiện định kỳ, đủ để khoá vĩnh viễn input từ xa.
    unsigned long evBits = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), &evBits) < 0) return false;
    const bool hasKey = evBits & (1UL << EV_KEY);
    const bool hasRel = evBits & (1UL << EV_REL);
    return hasKey || hasRel;
}

} // namespace

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
        // Không phải lỗi tử vong — xem ⚠ "cần quyền đọc" ở LocalInputMonitor.h.
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
            // Không có gì để theo dõi: ngủ theo nhịp quét lại thay vì quay vòng
            // bận. poll() với 0 fd trên Linux vẫn tôn trọng timeout nên dùng luôn
            // nó cho gọn.
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
                // POLLERR/POLLHUP = thiết bị vừa bị rút. Quét lại ngay ở vòng sau
                // thay vì poll mãi một fd đã chết (poll trả về ngay lập tức với
                // fd hỏng → vòng xoáy bận).
                if (pfds[i].revents) lastScanUs = 0;
                continue;
            }
            const ssize_t rd = read(pfds[i].fd, evs, sizeof(evs));
            if (rd < ssize_t(sizeof(input_event))) continue;
            const size_t count = size_t(rd) / sizeof(input_event);
            for (size_t k = 0; k < count; ++k) {
                // SYN/MSC là sự kiện đóng gói và tiếng ồn của scancode thô, không
                // phải hành động của người dùng.
                if (evs[k].type == EV_KEY || evs[k].type == EV_REL || evs[k].type == EV_ABS)
                    sawInput = true;
            }
        }
        if (sawInput) lastUs_.store(NowUs(), std::memory_order_relaxed);
    }
}
