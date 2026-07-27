#pragma once
// =============================================================================
// SessionWindow.h — cửa sổ quản lý phiên chia sẻ đang chạy, phía HOST (khôi phục
// từ ff454de~1, xem main.cpp về bối cảnh).
//
// VẤN ĐỀ
//   Bấm Share xong là màn hình chính ẩn đi và RunAgent chặn tới hết phiên —
//   không có cửa sổ này thì người dùng không biết đang chia sẻ gì, không
//   thêm/bớt được nguồn, muốn dừng chỉ có cách giết tiến trình.
//
// GIẢI PHÁP
//   Một cửa sổ nhỏ sống suốt phiên share: danh sách nguồn đang chia sẻ + ba nút
//   Add / Stop selected / Stop sharing.
//
// MÔ HÌNH LUỒNG — điểm quan trọng nhất của lớp này
//   Cửa sổ chạy trên MỘT THREAD UI RIÊNG (tự bơm message), vì thread gọi RunAgent
//   chính là vòng Recv — nó chặn ở recvfrom 100ms nên không bơm message được.
//   Hai thread nói chuyện qua hộp thư có mutex, KHÔNG gọi thẳng vào nhau:
//     UI  → Recv : stopReq_ — vòng Recv rút mỗi vòng lặp.
//     Recv → UI  : SetRows(danh sách nguồn) + cờ dirty; timer ~300ms đổ listbox.
//
// LIÊN QUAN: AgentLoop.cpp (nơi đẩy danh sách nguồn sang),
//            MainMenuWindow.cpp (nơi ẩn màn chính)
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AgentControl.h" // interface RunAgent điều khiển phiên
#include "AgentLoop.h"    // AgentSource
#include "SessionRow.h"   // SessionSourceRow

// Bản cài đặt AgentControl bằng cửa sổ Win32. Các method active/stopRequested/
// SetRows/TakeAdds/TakeRemoves chính là các override của interface.
class SessionWindow : public AgentControl {
public:
    SessionWindow() = default;
    ~SessionWindow() {
        Stop();
    }
    SessionWindow(const SessionWindow&) = delete;
    SessionWindow& operator=(const SessionWindow&) = delete;

    // Mở cửa sổ trên thread UI riêng, trả về ngay. Không tham số: cổng là hằng số và
    // trần số nguồn không còn ai cần (nút Add đã bỏ — danh sách chốt lúc bắt đầu).
    void Start();

    // Đóng cửa sổ và join thread UI. Gọi được nhiều lần / khi chưa Start.
    void Stop();

    bool active() const override {
        return active_.load(std::memory_order_acquire);
    }
    bool stopRequested() const override {
        return stopReq_.load(std::memory_order_acquire);
    }
    void SetRows(std::vector<SessionSourceRow> rows) override;

private:
    void ThreadMain();
    LRESULT HandleMsg(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK WndProcThunk(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    void RefreshList();

    std::thread thread_;
    std::atomic<bool> active_{false};
    std::atomic<bool> stopReq_{false};
    // Stop() đặt cờ này TRƯỚC khi post message đóng: nếu Stop chạy sớm hơn lúc
    // cửa sổ tạo xong (hwnd_ còn null) thì ThreadMain thấy cờ ngay sau khi tạo
    // và tự đóng — không thì join() treo vĩnh viễn.
    std::atomic<bool> quitReq_{false};
    std::atomic<HWND> hwnd_{nullptr};

    // --- Hộp thư giữa thread Recv và thread UI, mutex bảo vệ cả hai ---
    // Chỉ còn MỘT chiều (Recv → UI). Chiều ngược lại (adds_/removes_ cho nút Add /
    // Stop selected) đã bỏ 2026-07-27 cùng hai nút đó.
    std::mutex m_;
    std::vector<SessionSourceRow> rows_;
    bool dirty_ = false;

    // --- Chỉ thread UI chạm ---
    std::vector<SessionSourceRow> uiRows_;
    HWND list_ = nullptr;
};
