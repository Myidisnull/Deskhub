// =============================================================================
// main.cpp — điểm vào app Ubuntu. Đối ứng client/windows/win32/main.cpp.
//
// MỘT APP, CẢ HAI VAI
//   Giống Windows và macOS: một tệp thực thi duy nhất chứa cả vai HOST (chia sẻ)
//   lẫn vai CLIENT (xem), chọn lúc chạy bằng nút bấm. Không có tiến trình nền,
//   không có dịch vụ hệ thống, không có tài khoản.
//
// KHÔNG CÓ THAM SỐ DÒNG LỆNH
//   Bản Windows từng có --serve/--connect từ thời chưa có UI; ở đây bỏ hẳn để
//   không phải duy trì hai đường vào. Muốn kịch bản hoá thì gọi thẳng các lớp
//   trong client/linux/cpp — chúng không phụ thuộc GTK.
//
// ⚠ VÌ SAO GỌI XInitThreads KHÔNG CẦN THIẾT Ở ĐÂY
//   Nhiều app GTK cũ phải gọi nó vì chúng vẽ từ nhiều thread. Ta thì KHÔNG: mọi
//   lời gọi GTK/GDK/GL đều nằm trên main thread (xem ranh giới ở
//   render/VideoRenderer.h), các thread nền chỉ ném việc về qua RunOnMain.
//
// LIÊN QUAN: gtk/MainWindow.h, docs/17-linux-app.md
// =============================================================================
#include <gtk/gtk.h>

#include "gtk/MainWindow.h"

namespace {

void OnActivate(GtkApplication* app, gpointer) {
    MainWindow::Open(app);
}

} // namespace

int main(int argc, char** argv) {
    // NON_UNIQUE: mỗi lần chạy là một tiến trình riêng. Mặc định GtkApplication gom
    // các lần chạy về một tiến trình, và điều đó SAI ở đây — cổng UDP 47777 chỉ
    // một tiến trình bind được, nên hai bản chạy song song phải thấy lỗi "cổng đã
    // bận" một cách rõ ràng thay vì lặng lẽ nhập vào bản đang chạy.
    GtkApplication* app = gtk_application_new("com.manhpham.deskhub",
        G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(OnActivate), nullptr);
    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
