// =============================================================================
// ShareWindow.cpp — bảng trạng thái vai host.
//
// LIÊN QUAN: gtk/ShareWindow.h (bố cục, ⚠ vì sao không có nút thêm/bớt nguồn),
//            client/windows/win32/SessionWindow.cpp (bản tham chiếu)
// =============================================================================
#include "gtk/ShareWindow.h"

#include <cstdio>
#include <utility>

#include "capture/SourceEnum.h"
#include "gtk/GtkUtil.h"
#include "deskhubp/UdpSocket.h"

namespace {

// Cỡ cố định, chép từ SessionWindow.cpp (kW/kH). Cửa sổ này chỉ có một danh sách
// ngắn và một nút — cho kéo cỡ chẳng để làm gì.
constexpr int kWinW = 460;
constexpr int kWinH = 330;

// Bao hình của TOÀN BỘ desktop, đo bằng GDK. InputInjector cần nó để quy đổi toạ
// độ chuột tuyệt đối — xem input/InputInjector.h. GỌI TRÊN MAIN THREAD: GDK không
// an toàn ở nơi khác.
void DesktopBounds(int32_t& x, int32_t& y, uint32_t& w, uint32_t& h) {
    x = y = 0;
    w = h = 0;
    GdkDisplay* d = gdk_display_get_default();
    if (!d) return;
    const int n = gdk_display_get_n_monitors(d);
    if (n <= 0) return;

    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (int i = 0; i < n; ++i) {
        GdkMonitor* m = gdk_display_get_monitor(d, i);
        if (!m) continue;
        GdkRectangle r{};
        gdk_monitor_get_geometry(m, &r);
        if (i == 0) {
            minX = r.x;
            minY = r.y;
            maxX = r.x + r.width;
            maxY = r.y + r.height;
        } else {
            minX = r.x < minX ? r.x : minX;
            minY = r.y < minY ? r.y : minY;
            maxX = r.x + r.width > maxX ? r.x + r.width : maxX;
            maxY = r.y + r.height > maxY ? r.y + r.height : maxY;
        }
    }
    x = minX;
    y = minY;
    w = uint32_t(maxX - minX);
    h = uint32_t(maxY - minY);
}

// Góc dưới-phải vùng làm việc, giống chỗ SessionWindow tự đặt mình: cửa sổ này
// không được nằm giữa màn hình đang bị quay.
//
// ⚠ Trên Wayland gốc, gtk_window_move là lệnh rỗng — compositor giữ quyền đặt cửa
//   sổ. Cùng hạng với giới hạn warp con trỏ ở ViewerWindow.h: chạy đúng trên
//   X11/XWayland, bỏ qua ở chỗ khác. Không có đường nào khác qua GTK3.
void PlaceBottomRight(GtkWindow* win) {
    GdkDisplay* d = gdk_display_get_default();
    if (!d) return;
    GdkMonitor* m = gdk_display_get_primary_monitor(d);
    if (!m) m = gdk_display_get_monitor(d, 0);
    if (!m) return;
    GdkRectangle wa{};
    gdk_monitor_get_workarea(m, &wa);
    gtk_window_move(win, wa.x + wa.width - kWinW - 24, wa.y + wa.height - kWinH - 24);
}

// Một dòng danh sách, y khuôn bản Windows (AgentLoop.cpp dựng `label` cùng dạng).
GtkWidget* MakeRow(const char* text, const char* tooltip) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_end(label, 6);
    if (tooltip) gtk_widget_set_tooltip_text(label, tooltip);
    return label;
}

} // namespace

void ShareWindow::Open(const std::vector<ShareSource>& sources, const AgentOptions& opt,
    std::function<void()> onClosed) {
    auto* s = new ShareWindow();
    s->onClosed_ = std::move(onClosed);
    s->Build(sources, opt);
}

ShareWindow::~ShareWindow() {
    if (timer_) g_source_remove(timer_);
    if (starter_.joinable()) starter_.join();
    agent_.Stop();
    // Trả phiên portal: chỉ báo "đang quay màn hình" của compositor tắt ngay thay
    // vì sáng tới lúc app thoát.
    ReleaseShareSources();
}

void ShareWindow::Build(const std::vector<ShareSource>& sources, const AgentOptions& optIn) {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "Deskhub - sharing");
    gtk_window_set_default_size(GTK_WINDOW(window_), kWinW, kWinH);
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
    PlaceBottomRight(GTK_WINDOW(window_));
    g_signal_connect(window_, "destroy", G_CALLBACK(OnDestroy), this);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(window_), box);

    GtkWidget* caption = gtk_label_new("Sources currently being shared:");
    gtk_label_set_xalign(GTK_LABEL(caption), 0.f);
    gtk_box_pack_start(GTK_BOX(box), caption, FALSE, FALSE, 0);

    // GtkFrame + GtkScrolledWindow = cái khung có viền của LISTBOX bên Win32.
    GtkWidget* frame = gtk_frame_new(nullptr);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 0);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(frame), scroll);

    rowsBox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(rowsBox_, 4);
    gtk_widget_set_margin_bottom(rowsBox_, 4);
    gtk_container_add(GTK_CONTAINER(scroll), rowsBox_);
    // Start() còn đang chạy: chỗ này là đối ứng "Starting…" — cửa sổ phải có mặt
    // ngay chứ không đợi frame đầu.
    gtk_container_add(GTK_CONTAINER(rowsBox_), MakeRow("(starting…)", nullptr));

    GtkWidget* hint = gtk_label_new("Others connect by entering this machine's IP address.");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.f);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    // Nút canh PHẢI, như BS_PUSHBUTTON đặt ở kW-12-130 bên Win32.
    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget* stop = gtk_button_new_with_label("Stop sharing");
    gtk_widget_set_size_request(stop, 130, 28);
    g_signal_connect(stop, "clicked", G_CALLBACK(OnStopClicked), this);
    gtk_box_pack_start(GTK_BOX(buttons), stop, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    gtk_widget_show_all(window_);

    // --- Khởi động trên thread nền (xem "luồng khởi động" ở ShareWindow.h) ---
    AgentOptions opt = optIn;
    DesktopBounds(opt.desktopX, opt.desktopY, opt.desktopW, opt.desktopH);

    std::vector<AgentSource> agentSources;
    agentSources.reserve(sources.size());
    for (const ShareSource& s : sources) {
        AgentSource a;
        a.nodeId = s.nodeId;
        a.name = s.name;
        a.x = s.x;
        a.y = s.y;
        a.width = s.width;
        a.height = s.height;
        agentSources.push_back(std::move(a));
    }

    starter_ = std::thread([this, agentSources, opt, alive = alive_] {
        const bool ok = agent_.Start(agentSources, opt);
        const std::string err = ok ? std::string() : agent_.LastError();
        // `alive` chứ không phải `this`: xem ⚠ ở ShareWindow.h. Thân thread vẫn
        // dùng `this` được vì destructor join thread này trước khi delete; chỉ
        // lambda chạy SAU trên main loop mới cần cờ.
        RunOnMain([this, ok, err, alive] {
            if (!alive->load()) return;
            starting_ = false;
            if (!ok) {
                ShowError(GTK_WINDOW(window_), "Could not start sharing", err);
                gtk_widget_destroy(window_);
                return;
            }
            Refresh();
        });
    });

    timer_ = g_timeout_add(500, OnTimer, this);
}

gboolean ShareWindow::OnTimer(gpointer user) {
    static_cast<ShareWindow*>(user)->Refresh();
    return G_SOURCE_CONTINUE;
}

void ShareWindow::Refresh() {
    if (starting_ || !window_) return;

    // Phiên tự kết thúc (mất hết nguồn, lỗi socket) — đóng cửa sổ thay vì để nó
    // đứng đó hiện số liệu đã đóng băng.
    if (!agent_.running()) {
        gtk_widget_destroy(window_);
        return;
    }

    RefreshList(agent_.Status());
}

// Dựng lại toàn bộ danh sách mỗi lượt. Với vài hàng thì đây là cách đơn giản nhất
// và chi phí không đáng kể — GTK dựng lại một GtkLabel rẻ hơn nhiều so với chi phí
// giữ đồng bộ hai danh sách. (Bản Win32 phải nhớ sourceId để giữ dòng đang chọn;
// ở đây danh sách không chọn được nên không có gì phải giữ.)
void ShareWindow::RefreshList(const std::vector<AgentSourceStatus>& rows) {
    GList* kids = gtk_container_get_children(GTK_CONTAINER(rowsBox_));
    for (GList* l = kids; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    if (rows.empty()) {
        gtk_container_add(GTK_CONTAINER(rowsBox_), MakeRow("(nothing is being shared)", nullptr));
        gtk_widget_show_all(rowsBox_);
        return;
    }

    for (const AgentSourceStatus& r : rows) {
        char text[320];
        std::snprintf(text, sizeof(text), "%s  (%ux%u%s)", r.name.c_str(), r.width, r.height,
            r.viewerConnected ? ", viewer connected" : "");

        // Số liệu chi tiết ở tooltip: bố cục vẫn y bản Windows, thông tin chẩn đoán
        // vẫn còn (docs/17 §6 dựa vào chỗ này để biết đang đi đường zero-copy hay
        // đường chép qua RAM).
        char tip[512];
        if (r.viewerConnected)
            std::snprintf(tip, sizeof(tip),
                "viewer %s\n%.0f fps sent · %.0f kbps · RTT %u ms\ncapture %.0f fps · %s\n"
                "UDP port %u",
                r.viewerAddr.c_str(), r.sendFps, r.sendKbps, r.rttMs, r.captureFps,
                r.zeroCopy ? "zero-copy" : "CPU copy", unsigned(kDeskhubPort));
        else
            std::snprintf(tip, sizeof(tip),
                "waiting for a viewer\ncapture %.0f fps · %s\nUDP port %u", r.captureFps,
                r.zeroCopy ? "zero-copy" : "CPU copy", unsigned(kDeskhubPort));

        gtk_container_add(GTK_CONTAINER(rowsBox_), MakeRow(text, tip));
    }
    gtk_widget_show_all(rowsBox_);
}

void ShareWindow::OnStopClicked(GtkButton*, gpointer user) {
    auto* self = static_cast<ShareWindow*>(user);
    gtk_widget_destroy(self->window_);
}

void ShareWindow::OnDestroy(GtkWidget*, gpointer user) {
    auto* self = static_cast<ShareWindow*>(user);
    self->alive_->store(false); // vô hiệu hoá mọi lambda đang bay
    self->window_ = nullptr;
    if (self->timer_) {
        g_source_remove(self->timer_);
        self->timer_ = 0;
    }
    // Lấy callback RA TRƯỚC khi delete: nó thuộc về đối tượng sắp biến mất, và
    // MainWindow hiện lại phải là việc CUỐI (destructor còn join thread khởi động
    // và trả phiên portal — người dùng không nên thấy màn hình chính trước đó).
    auto done = std::move(self->onClosed_);
    delete self;
    if (done) done();
}
