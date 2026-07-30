#pragma once
// =============================================================================
// MainWindow.h — màn hình chính. Đối ứng client/windows/win32/MainMenuWindow.h và
//                ContentView.swift bên macOS.
//
// BỐ CỤC CHÉP THEO MainMenuWindow.cpp (chốt 2026-07-30) — HAI HỘP
//   • "Host mode"   — chia sẻ máy này: danh sách IPv4 (mỗi dòng một nút Copy),
//                     UDP port 47777 · FPS · Bitrate, nút Share.
//   • "Client mode" — kết nối máy khác: ô IP, nút Connect.
//   Dưới cùng là nút Exit. Cửa sổ KHÔNG đổi cỡ được, y bản Windows.
//   KHÔNG có ô Port và KHÔNG có ô "View only": cổng luôn là kDeskhubPort và
//   chuột/bàn phím luôn được chia sẻ, nên cả hai chỉ còn là chữ chứ không phải
//   lựa chọn. Không có light/dark, không có đa ngôn ngữ.
//
// CỬA SỔ CHÍNH ẨN TRONG LÚC PHIÊN CHẠY
//   Bên Windows, DoShare/DoConnect gọi ShowWindow(SW_HIDE), CHẶN suốt phiên, rồi
//   SW_SHOW. Ở đây không chặn được (GTK main loop), nên cùng hiệu ứng làm bằng
//   callback: ẩn khi mở phiên, hiện lại khi ShareWindow đóng — hoặc khi cửa sổ xem
//   CUỐI CÙNG đóng (`openViewers_` đếm, đối ứng g_openFrames của Viewer.cpp).
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
#include <string>
#include <vector>

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
    // Hai hộp của bố cục Windows, mỗi cái một hàm dựng.
    GtkWidget* BuildHostBox();
    GtkWidget* BuildClientBox();

    void SetBusy(bool busy, const char* what);
    // Ẩn/hiện cửa sổ chính quanh một phiên (xem đầu file).
    void HideForSession();
    void ShowAfterSession();

    static void OnShareClicked(GtkButton* b, gpointer user);
    static void OnConnectClicked(GtkButton* b, gpointer user);
    static void OnAddressActivate(GtkEntry* e, gpointer user);
    static void OnCopyClicked(GtkButton* b, gpointer user);
    static void OnExitClicked(GtkButton* b, gpointer user);
    static void OnDestroy(GtkWidget* w, gpointer user);

    GtkWidget* window_ = nullptr;
    GtkWidget* addressEntry_ = nullptr;
    GtkWidget* fpsEntry_ = nullptr;
    GtkWidget* bitrateEntry_ = nullptr;
    GtkWidget* shareButton_ = nullptr;
    GtkWidget* connectButton_ = nullptr;
    GtkWidget* statusLabel_ = nullptr;

    // Số cửa sổ xem đang mở. Về 0 thì cửa sổ chính hiện lại.
    int openViewers_ = 0;

    // Cùng lý do với ShareWindow::alive_ — lambda chạy trên main loop SAU khi cửa
    // sổ có thể đã đóng.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
