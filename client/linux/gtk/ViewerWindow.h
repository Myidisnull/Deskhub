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

#include <memory>
#include <string>

#include "ClientLoop.h"
#include "net/UdpSocket.h"
#include "render/VideoRenderer.h"

class ViewerWindow {
public:
    // Mở cửa sổ và bắt đầu phiên ngay. `title` hiện trên thanh tiêu đề.
    // Trả về nullptr nếu không mở được socket. Đối tượng TỰ HUỶ khi cửa sổ đóng —
    // caller không giữ con trỏ.
    static ViewerWindow* Open(const NetAddr& server, uint8_t sourceId, const std::string& title);

private:
    ViewerWindow() = default;
    ~ViewerWindow();
    ViewerWindow(const ViewerWindow&) = delete;
    ViewerWindow& operator=(const ViewerWindow&) = delete;

    bool Build(const NetAddr& server, uint8_t sourceId, const std::string& title);

    // Vùng CHỮ NHẬT VIDEO thật bên trong GtkGLArea (đã trừ viền đen). Phải khớp
    // đúng phép căn khung của VideoRenderer::Render, nếu không con trỏ sẽ lệch khi
    // tỉ lệ cửa sổ khác tỉ lệ video.
    void VideoRect(int& x, int& y, int& w, int& h) const;
    // Toạ độ con trỏ trong widget → toạ độ chuẩn hoá 0..65535 trong khung video.
    // false = con trỏ nằm ngoài vùng video (trên viền đen) → không gửi gì.
    bool ToNormalized(double px, double py, int32_t& nx, int32_t& ny) const;

    void SetPointerLocked(bool locked);
    void UpdateOverlay();

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
    GtkWidget* overlayLabel_ = nullptr;
    guint statusTimer_ = 0;
    guint tickId_ = 0;

    // ⚠ THỨ TỰ NÀY QUAN TRỌNG — xem đầu file.
    VideoRenderer renderer_;
    ClientLoop loop_;

    bool pointerLocked_ = false;
    bool inputPaused_ = false;
    // Vị trí con trỏ lần trước, để tính delta ở chế độ khoá chuột.
    double lastPx_ = 0, lastPy_ = 0;
    bool haveLastPos_ = false;
};
