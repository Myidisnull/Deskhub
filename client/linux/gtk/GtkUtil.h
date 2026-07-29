#pragma once
// =============================================================================
// GtkUtil.h — hai tiện ích nhỏ mà cả ba cửa sổ đều cần.
//
// VÌ SAO CẦN RunOnMain
//   Ba thao tác của app CHẶN lâu và vì thế phải chạy trên thread nền:
//     - GetShareSources()  — chờ người dùng bấm trong hộp thoại portal.
//     - QuerySources()     — chờ host trả lời, tới 3 giây.
//     - AgentLoop::Start() — chờ frame đầu của từng màn hình.
//   Nhưng GTK thì TUYỆT ĐỐI chỉ được chạm từ main thread. RunOnMain là cây cầu:
//   thread nền làm xong việc rồi ném một lambda về main loop.
//
// ⚠ VÒNG ĐỜI CỦA LAMBDA
//   g_idle_add chạy lambda ở một thời điểm SAU. Mọi thứ nó bắt theo tham chiếu
//   phải còn sống tới lúc đó — bắt `this` của một cửa sổ có thể đã đóng là dùng bộ
//   nhớ đã giải phóng. Quy ước trong app này: chỉ bắt theo GIÁ TRỊ, và nếu cần
//   chạm vào cửa sổ thì bắt con trỏ GtkWidget* đã g_object_ref.
//
// LIÊN QUAN: gtk/MainWindow.cpp, gtk/ShareWindow.cpp, gtk/ViewerWindow.cpp
// =============================================================================
#include <gtk/gtk.h>

#include <functional>
#include <utility>

// Chạy `fn` trên main loop của GTK. An toàn gọi từ thread bất kỳ.
inline void RunOnMain(std::function<void()> fn) {
    auto* boxed = new std::function<void()>(std::move(fn));
    g_idle_add(
        [](gpointer data) -> gboolean {
            auto* f = static_cast<std::function<void()>*>(data);
            (*f)();
            delete f;
            return G_SOURCE_REMOVE;
        },
        boxed);
}

// Hộp thoại lỗi một nút. `parent` có thể là nullptr.
inline void ShowError(GtkWindow* parent, const char* title, const std::string& detail) {
    GtkWidget* dlg = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE, "%s", title);
    if (!detail.empty())
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", detail.c_str());
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}
