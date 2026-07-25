// =============================================================================
// AgentApi.cpp — cài đặt C API vai HOST (xem DeskhubApi.h §VAI HOST).
//
// Ghép hai mảnh đã có: vòng lặp RunAgent (AgentLoop.cpp) + interface AgentControl.
// HeadlessAgentControl là bản cài đặt AgentControl KHÔNG có cửa sổ Win32 — nó chỉ là
// một hộp thư an toàn luồng: C# đẩy lệnh add/remove/stop vào, RunAgent (thread nền)
// rút ra; RunAgent đẩy danh sách nguồn ra, chuyển sang UTF-8 rồi gọi callback về C#.
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "DeskhubApi.h"

#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AgentControl.h"
#include "AgentLoop.h"
#include "capture/WindowCapture.h" // CaptureTarget + capture::InitRuntime

namespace {

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0,
        nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// AgentControl không cửa sổ: hộp thư có mutex giữa thread Recv (RunAgent) và các lời
// gọi C API từ thread UI của C#. Cùng khuôn với SessionWindow nhưng bỏ hết phần Win32.
class HeadlessAgentControl : public AgentControl {
public:
    HeadlessAgentControl(DhAgentRowsCallback rowsCb, DhAgentBoundCallback boundCb, void* user)
        : rowsCb_(rowsCb), boundCb_(boundCb), user_(user) {}

    // --- Phía RunAgent (thread Recv) gọi ---
    bool active() const override {
        return !stop_.load(std::memory_order_acquire);
    }
    bool stopRequested() const override {
        return stop_.load(std::memory_order_acquire);
    }

    void SetRows(std::vector<SessionSourceRow> rows) override {
        if (!rowsCb_) return;
        // Mọi chuỗi UTF-8 phải sống tới HẾT lời gọi callback, mà DhAgentRow chỉ giữ
        // con trỏ — nên các vector dưới đây phải được khai báo ở đây và reserve TRƯỚC
        // khi lấy .c_str(). Thiếu reserve thì push_back sẽ cấp lại bộ nhớ và mọi con
        // trỏ đã lấy thành con trỏ treo.
        std::vector<std::string> labels, names, viewers;
        labels.reserve(rows.size());
        names.reserve(rows.size());
        viewers.reserve(rows.size());
        for (const auto& r : rows) {
            labels.push_back(ToUtf8(r.label));
            names.push_back(ToUtf8(r.name));
            viewers.push_back(r.viewerAddr);
        }

        std::vector<DhAgentRow> crows;
        crows.reserve(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            crows.push_back(DhAgentRow{r.sourceId, labels[i].c_str(), r.pending ? 1 : 0,
                names[i].c_str(), r.width, r.height, r.isDisplay ? 1 : 0,
                r.viewerConnected ? 1 : 0, viewers[i].c_str(), r.fps, r.kbps, r.rttMs});
        }
        rowsCb_(crows.data(), int(crows.size()), user_);
    }

    std::vector<AgentSource> TakeAdds() override {
        std::lock_guard<std::mutex> lk(m_);
        return std::move(adds_); // adds_ về rỗng sau move
    }
    std::vector<uint8_t> TakeRemoves() override {
        std::lock_guard<std::mutex> lk(m_);
        return std::move(removes_);
    }
    void OnBound(uint16_t port) override {
        if (boundCb_) boundCb_(port, user_);
    }

    // --- Phía C API (thread UI của C#) gọi ---
    void RequestStop() {
        stop_.store(true, std::memory_order_release);
    }
    void QueueAdd(AgentSource s) {
        std::lock_guard<std::mutex> lk(m_);
        adds_.push_back(std::move(s));
    }
    void QueueRemove(uint8_t id) {
        std::lock_guard<std::mutex> lk(m_);
        removes_.push_back(id);
    }

private:
    DhAgentRowsCallback rowsCb_;
    DhAgentBoundCallback boundCb_;
    void* user_;
    std::atomic<bool> stop_{false};
    std::mutex m_;
    std::vector<AgentSource> adds_; // C# ghi, RunAgent rút
    std::vector<uint8_t> removes_;  // C# ghi, RunAgent rút
};

AgentSource ToAgentSource(const DhAgentSource& s) {
    AgentSource out;
    if (s.hwnd)
        out.target = CaptureTarget::Window(reinterpret_cast<HWND>(uintptr_t(s.hwnd)));
    else if (s.monitor)
        out.target = CaptureTarget::Monitor(reinterpret_cast<HMONITOR>(uintptr_t(s.monitor)));
    out.name = s.name ? s.name : "";
    return out;
}

} // namespace

// Handle mờ: gói control + thread + danh sách nguồn ban đầu (span của RunAgent trỏ vào
// vector này nên nó phải sống suốt phiên).
struct DhAgentHandle {
    HeadlessAgentControl ctl;
    std::vector<AgentSource> initialSources;
    std::thread thread;

    DhAgentHandle(DhAgentRowsCallback r, DhAgentBoundCallback b, void* u) : ctl(r, b, u) {}
};

DH_API DhAgentHandle* DH_CALL dh_agent_start(const DhAgentSource* sources, int count,
    const DhAgentOptions* opt, DhAgentRowsCallback rowsCb, DhAgentBoundCallback boundCb,
    void* user) {
    if (!sources || count <= 0 || !opt) return nullptr;

    auto* h = new DhAgentHandle(rowsCb, boundCb, user);
    for (int i = 0; i < count; ++i) h->initialSources.push_back(ToAgentSource(sources[i]));

    AgentOptions ao;
    ao.port = opt->port;
    ao.fps = opt->fps;
    ao.bitrateMbps = opt->bitrateMbps;
    ao.allowInput = opt->allowInput != 0;
    ao.shareClipboard = opt->shareClipboard != 0;

    h->thread = std::thread([h, ao] {
        // WGC cần WinRT (MTA) trên chính thread tạo WindowCapture — đây là thread đó.
        capture::InitRuntime();
        RunAgent(h->initialSources, ao, h->ctl);
    });
    return h;
}

DH_API void DH_CALL dh_agent_add_window(DhAgentHandle* h, uint64_t hwnd, const char* name) {
    if (!h || !hwnd) return;
    AgentSource s;
    s.target = CaptureTarget::Window(reinterpret_cast<HWND>(uintptr_t(hwnd)));
    s.name = name ? name : "";
    h->ctl.QueueAdd(std::move(s));
}

DH_API void DH_CALL dh_agent_remove(DhAgentHandle* h, uint8_t sourceId) {
    if (!h) return;
    h->ctl.QueueRemove(sourceId);
}

DH_API void DH_CALL dh_agent_stop(DhAgentHandle* h) {
    if (!h) return;
    h->ctl.RequestStop();
    if (h->thread.joinable()) h->thread.join();
    delete h;
}
