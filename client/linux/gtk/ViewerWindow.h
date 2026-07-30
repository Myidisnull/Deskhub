#pragma once
// =============================================================================
// ViewerWindow.h — cửa sổ XEM một nguồn. Đối ứng client/windows/win32/Viewer.h +
//                  ViewerInput.h, và StreamView.swift bên macOS.
//
// NHIỆM VỤ
//   Một GtkWindow chứa GtkGLArea, cộng toàn bộ việc bắt chuột/bàn phím rồi bơm vào
//   ClientLoop. Mỗi nguồn đang xem là MỘT cửa sổ như thế (GĐ6: mỗi cặp
//   client-nguồn là một phiên độc lập).
//
// CẢ CỬA SỔ LÀ VIDEO — SỐ LIỆU NẰM Ở THANH TIÊU ĐỀ (chốt 2026-07-30, theo Viewer.cpp)
//   Không có overlay đè lên hình, không có nút Disconnect: đóng cửa sổ là ngắt.
//   Tiêu đề ghép đúng khuôn bản Windows:
//     "Deskhub - viewing: <nguồn> — <stats> · <gợi ý F9>"
//   Lần đầu biết cỡ video thì nới cửa sổ cho khung đúng tỉ lệ 1:1 (kẹp vào vùng
//   làm việc); sau đó người dùng kéo cỡ tuỳ ý, video tự fit.
//
// ⚠ THỨ TỰ HUỶ LÀ MỘT HỢP ĐỒNG, KHÔNG PHẢI CHI TIẾT NỘI BỘ
//   ClientLoop::Stop() phải chạy XONG (join cả thread Net lẫn Decode) TRƯỚC khi
//   VideoRenderer bị huỷ — xem ⚠ ở ClientLoop.h. Trong lớp này, hai thành viên
//   được khai báo theo thứ tự renderer_ rồi loop_, nên destructor huỷ NGƯỢC LẠI
//   (loop_ trước) đúng như hợp đồng đòi hỏi. ĐỪNG đảo thứ tự khai báo.
//
// BA PHÍM TẮT CỤC BỘ (không gửi đi, giống client Windows — docs/07 §4)
//   F9  — khoá/mở chuột: chuyển giữa chuột TUYỆT ĐỐI (trỏ đúng chỗ, dùng cho công
//         việc) và chuột TƯƠNG ĐỐI (delta thô, dùng cho game FPS).
//   F10 — tạm dừng/tiếp tục gửi input. Lối thoát khi cần gõ vào chính máy này.
//   Esc — trong chế độ khoá chuột thì nhả chuột ra; ngoài chế độ đó thì gửi đi
//         như phím bình thường.
//
// ⚠ GIỚI HẠN CỦA CHẾ ĐỘ KHOÁ CHUỘT TRÊN WAYLAND
//   Khoá chuột đúng nghĩa cần giao thức pointer-constraints + relative-pointer của
//   Wayland, mà GTK3 KHÔNG phơi ra. Ta làm được điều gần nhất: ẩn con trỏ, grab
//   seat, và sau mỗi lần di chuyển thì kéo con trỏ về giữa cửa sổ bằng
//   gdk_device_warp — thủ thuật này chạy trên X11/XWayland nhưng trên Wayland gốc
//   thì warp là lệnh rỗng, nên con trỏ vẫn chạm được mép cửa sổ và delta dừng lại
//   ở đó. Ghi rõ ở docs/17-linux-app.md §6; muốn chuẩn thì phải bỏ GTK3 hoặc gọi
//   thẳng libwayland.
//
// LIÊN QUAN: ClientLoop.h, render/VideoRenderer.h, input/LinuxKeyMap.h,
//            client/windows/win32/ViewerInput.h (bản tham chiếu desktop)
// =============================================================================
#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>

#include "ClientLoop.h"
#include "net/UdpSocket.h"
#include "render/VideoRenderer.h"

class ViewerWindow {
public:
    // Mở cửa sổ và bắt đầu phiên ngay. `sourceName` là tên nguồn (rỗng nếu host đời
    // cũ không trả lời LIST_SOURCES) — nó thành phần cố định của tiêu đề.
    // `onClosed` chạy trên main thread khi cửa sổ đóng; MainWindow đếm số cửa sổ còn
    // mở và hiện mình lại khi cái cuối cùng đóng, đối ứng chỗ RunViewer() trả về bên
    // Windows.
    // Trả về nullptr nếu không mở được socket. Đối tượng TỰ HUỶ khi cửa sổ đóng —
    // caller không giữ con trỏ.
    static ViewerWindow* Open(const NetAddr& server, uint8_t sourceId,
        const std::string& sourceName, std::function<void()> onClosed);

private:
    ViewerWindow() = default;
    ~ViewerWindow();
    ViewerWindow(const ViewerWindow&) = delete;
    ViewerWindow& operator=(const ViewerWindow&) = delete;

    bool Build(const NetAddr& server, uint8_t sourceId, const std::string& sourceName);

    // Vùng CHỮ NHẬT VIDEO thật bên trong GtkGLArea (đã trừ viền đen). Phải khớp
    // đúng phép căn khung của VideoRenderer::Render, nếu không con trỏ sẽ lệch khi
    // tỉ lệ cửa sổ khác tỉ lệ video.
    void VideoRect(int& x, int& y, int& w, int& h) const;
    // Toạ độ con trỏ trong widget → toạ độ chuẩn hoá 0..65535 trong khung video.
    // false = con trỏ nằm ngoài vùng video (trên viền đen) → không gửi gì.
    bool ToNormalized(double px, double py, int32_t& nx, int32_t& ny) const;

    // ⚠ CON TRỎ CÓ ĐANG Ở TRONG VÙNG NỘI DUNG KHÔNG — ĐỌC TRƯỚC KHI SỬA HANDLER CHUỘT
    //   Sự kiện chuột bắt trên TOPLEVEL, mà GTK3 vẽ decoration bằng CSD: thanh tiêu
    //   đề và viền kéo cỡ nằm TRÊN CÙNG GdkWindow với nội dung. Handler nối bằng
    //   g_signal_connect chạy TRƯỚC class handler của GtkWindow, nên trả TRUE cho
    //   một cú bấm ngoài vùng nội dung là chặn luôn gtk_window_button_press_event —
    //   và cửa sổ mất khả năng di chuyển lẫn đổi cỡ, không một lỗi nào được in ra.
    //   Vì thế: ngoài vùng này thì handler phải trả FALSE, để GTK xử lý tiếp.
    //   (Trừ lúc khoá chuột: khi đó seat đã bị grab, mọi sự kiện đều là của ta.)
    bool InContent(double px, double py) const;

    void SetPointerLocked(bool locked);
    // Ghép lại tiêu đề (tên nguồn + stats + gợi ý phím). So với chuỗi đang hiện rồi
    // mới đặt: SetTitle mỗi 500ms dù không đổi là bắt WM vẽ lại thanh tiêu đề vô ích.
    void UpdateTitle();
    // Lần đầu biết cỡ video: nới cửa sổ cho khung video đúng 1:1, kẹp vào work area.
    void SizeToVideo();
    // Phiên đứt từ phía dưới: báo một câu rồi đóng cửa sổ (giống WM_APP_CLOSED bên
    // Windows). Chỉ chạy MỘT lần — hộp thoại quay main loop lồng nhau, timer vẫn nổ.
    void EndSession();

    // --- Cầu nối tín hiệu GTK (đều là hàm tĩnh, `user` là ViewerWindow*) ---
    static gboolean OnRender(GtkGLArea* area, GdkGLContext* ctx, gpointer user);
    static void OnRealize(GtkGLArea* area, gpointer user);
    static void OnUnrealize(GtkGLArea* area, gpointer user);
    static gboolean OnKey(GtkWidget* w, GdkEventKey* e, gpointer user);
    static gboolean OnMotion(GtkWidget* w, GdkEventMotion* e, gpointer user);
    static gboolean OnButton(GtkWidget* w, GdkEventButton* e, gpointer user);
    static gboolean OnScroll(GtkWidget* w, GdkEventScroll* e, gpointer user);
    static gboolean OnFocusOut(GtkWidget* w, GdkEventFocus* e, gpointer user);
    static gboolean OnTick(GtkWidget* w, GdkFrameClock* clock, gpointer user);
    static gboolean OnStatusTimer(gpointer user);
    static void OnDestroy(GtkWidget* w, gpointer user);

    GtkWidget* window_ = nullptr;
    GtkWidget* glArea_ = nullptr;
    guint statusTimer_ = 0;
    guint tickId_ = 0;

    std::string baseTitle_;  // "Deskhub - viewing[: <nguồn>]" — phần cố định
    std::string shownTitle_; // tiêu đề đang hiện, để khỏi đặt lại vô ích
    bool sizedToVideo_ = false;
    bool ended_ = false;
    std::function<void()> onClosed_;

    // ⚠ THỨ TỰ NÀY QUAN TRỌNG — xem đầu file.
    VideoRenderer renderer_;
    ClientLoop loop_;

    bool pointerLocked_ = false;
    bool inputPaused_ = false;
    // Vị trí con trỏ lần trước, để tính delta ở chế độ khoá chuột.
    double lastPx_ = 0, lastPy_ = 0;
    bool haveLastPos_ = false;
};
