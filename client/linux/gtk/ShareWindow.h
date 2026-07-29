#pragma once
// =============================================================================
// ShareWindow.h — cửa sổ trạng thái khi máy này ĐANG CHIA SẺ.
//                 Đối ứng client/windows/win32/SessionWindow.h và ShareView.swift
//                 bên macOS.
//
// NHIỆM VỤ
//   Hiện một hàng cho mỗi màn hình đang chia sẻ (kích thước, ai đang xem, fps,
//   kbps, RTT), và một nút Stop. Đó là toàn bộ giao diện của vai host — mọi lựa
//   chọn khác đã được quyết ở hộp thoại của portal trước đó.
//
// ⚠ KHÔNG CÓ NÚT THÊM/BỚT NGUỒN
//   Danh sách nguồn chốt lúc Start và không đổi (chốt 2026-07-27, giống Windows/
//   macOS). Muốn đổi màn hình chia sẻ thì Stop rồi Share lại — và trên Wayland
//   điều đó là bắt buộc chứ không phải lựa chọn: đổi tập nguồn cần một phiên
//   portal mới, tức là một hộp thoại hệ thống mới.
//
// LUỒNG KHỞI ĐỘNG — HAI NHỊP, VÌ Start() CHẶN
//   AgentLoop::Start() phải đợi frame đầu của từng màn hình (tới 10 giây). Chặn
//   main thread chừng đó là cửa sổ xám ngoét. Nên: dựng cửa sổ + hiện "Starting…"
//   NGAY, rồi chạy Start() trên thread nền và cập nhật lại khi xong.
//
// LIÊN QUAN: AgentLoop.h, capture/SourceEnum.h, gtk/MainWindow.cpp (người mở)
// =============================================================================
#include <gtk/gtk.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "AgentLoop.h"
#include "capture/SourceEnum.h"

class ShareWindow {
public:
    // Mở cửa sổ và bắt đầu chia sẻ `sources` (đã được portal cấp quyền).
    // Đối tượng TỰ HUỶ khi cửa sổ đóng.
    static void Open(const std::vector<ShareSource>& sources);

private:
    ShareWindow() = default;
    ~ShareWindow();
    ShareWindow(const ShareWindow&) = delete;
    ShareWindow& operator=(const ShareWindow&) = delete;

    void Build(const std::vector<ShareSource>& sources);
    void Refresh();

    static gboolean OnTimer(gpointer user);
    static void OnStopClicked(GtkButton* b, gpointer user);
    static void OnDestroy(GtkWidget* w, gpointer user);

    GtkWidget* window_ = nullptr;
    GtkWidget* headline_ = nullptr;
    GtkWidget* rowsBox_ = nullptr;
    guint timer_ = 0;

    AgentLoop agent_;
    std::thread starter_;
    bool starting_ = true;

    // ⚠ CỜ SỐNG/CHẾT DÙNG CHUNG VỚI LAMBDA ĐANG BAY.
    // Thread khởi động ném một lambda về main loop khi Start() xong. Cửa sổ có thể
    // đã bị đóng trước lúc lambda đó được g_idle chạy — và lúc ấy `this` đã bị
    // delete. Không thể chỉ dựa vào việc join thread: join bảo đảm THREAD đã xong,
    // không bảo đảm CALLBACK đã chạy. shared_ptr này sống độc lập với đối tượng,
    // lambda bắt nó THEO GIÁ TRỊ và kiểm tra trước khi chạm vào bất cứ thứ gì.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
