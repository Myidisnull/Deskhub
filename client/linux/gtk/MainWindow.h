#pragma once
// =============================================================================
// MainWindow.h — màn hình chính. Đối ứng client/windows/win32/MainMenuWindow.h và
//                ContentView.swift bên macOS.
//
// TOÀN BỘ GIAO DIỆN CỦA APP LÀ HAI NÚT (chốt 2026-07-27, giống mọi nền tảng khác)
//   Share   — chia sẻ màn hình của máy này.
//   Connect — gõ IP của máy kia rồi xem.
//   Cộng thêm danh sách địa chỉ IPv4 của máy này, để đọc cho người ở đầu bên kia.
//   Không có light/dark, không có đa ngôn ngữ, không có chế độ chỉ-xem.
//
// ⚠ CẢ HAI NÚT ĐỀU CHẠY TRÊN THREAD NỀN
//   Share  → GetShareSources() mở hộp thoại portal và chờ người dùng bấm.
//   Connect → QuerySources() chờ host trả lời tới 3 giây.
//   Cả hai mà chạy trên main thread thì cửa sổ đứng hình. Xem gtk/GtkUtil.h.
//
// LIÊN QUAN: gtk/ShareWindow.h, gtk/ViewerWindow.h, capture/SourceEnum.h,
//            net/SourceQuery.h, client/windows/win32/MainMenuWindow.h
// =============================================================================
#include <gtk/gtk.h>

#include <atomic>
#include <memory>

class MainWindow {
public:
    // Dựng và hiện cửa sổ chính. `app` là GtkApplication đang chạy.
    static void Open(GtkApplication* app);

private:
    MainWindow() = default;
    ~MainWindow() = default;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void Build(GtkApplication* app);
    void SetBusy(bool busy, const char* what);

    static void OnShareClicked(GtkButton* b, gpointer user);
    static void OnConnectClicked(GtkButton* b, gpointer user);
    static void OnAddressActivate(GtkEntry* e, gpointer user);
    static void OnDestroy(GtkWidget* w, gpointer user);

    GtkWidget* window_ = nullptr;
    GtkWidget* addressEntry_ = nullptr;
    GtkWidget* shareButton_ = nullptr;
    GtkWidget* connectButton_ = nullptr;
    GtkWidget* statusLabel_ = nullptr;

    // Cùng lý do với ShareWindow::alive_ — lambda chạy trên main loop SAU khi cửa
    // sổ có thể đã đóng.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
