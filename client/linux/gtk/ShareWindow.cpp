// =============================================================================
// ShareWindow.cpp — bảng trạng thái vai host.
//
// LIÊN QUAN: gtk/ShareWindow.h (⚠ vì sao không có nút thêm/bớt nguồn)
// =============================================================================
#include "gtk/ShareWindow.h"

#include <cstdio>

#include "Log.h"
#include "capture/SourceEnum.h"
#include "gtk/GtkUtil.h"
#include "net/NetInfo.h"
#include "net/UdpSocket.h"

namespace {

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

} // namespace

void ShareWindow::Open(const std::vector<ShareSource>& sources) {
    auto* s = new ShareWindow();
    s->Build(sources);
}

ShareWindow::~ShareWindow() {
    if (timer_) g_source_remove(timer_);
    if (starter_.joinable()) starter_.join();
    agent_.Stop();
    // Trả phiên portal: chỉ báo "đang quay màn hình" của compositor tắt ngay thay
    // vì sáng tới lúc app thoát.
    ReleaseShareSources();
}

void ShareWindow::Build(const std::vector<ShareSource>& sources) {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "Deskhub — sharing");
    gtk_window_set_default_size(GTK_WINDOW(window_), 620, 320);
    g_signal_connect(window_, "destroy", G_CALLBACK(OnDestroy), this);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);
    gtk_container_add(GTK_CONTAINER(window_), box);

    headline_ = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(headline_), 0.f);
    gtk_label_set_use_markup(GTK_LABEL(headline_), TRUE);
    gtk_label_set_markup(GTK_LABEL(headline_), "<b>Starting…</b>");
    gtk_box_pack_start(GTK_BOX(box), headline_, FALSE, FALSE, 0);

    // Địa chỉ để gõ ở máy bên kia. Hiện NGAY, trước cả khi phiên chạy: đó là thứ
    // người dùng cần đọc cho người kia, và họ không nên phải chờ.
    GtkWidget* addrLabel = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(addrLabel), 0.f);
    gtk_label_set_selectable(GTK_LABEL(addrLabel), TRUE);
    std::string addrs = "On the other machine, enter one of these addresses:\n";
    for (const auto& a : ListLocalIPv4()) addrs += "    " + a.ip + "    (" + a.name + ")\n";
    if (addrs.find("    ") == std::string::npos) addrs += "    (no network interface is up)\n";
    gtk_label_set_text(GTK_LABEL(addrLabel), addrs.c_str());
    gtk_box_pack_start(GTK_BOX(box), addrLabel, FALSE, FALSE, 0);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    rowsBox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(scroll), rowsBox_);

    GtkWidget* stop = gtk_button_new_with_label("Stop sharing");
    g_signal_connect(stop, "clicked", G_CALLBACK(OnStopClicked), this);
    gtk_box_pack_start(GTK_BOX(box), stop, FALSE, FALSE, 0);

    gtk_widget_show_all(window_);

    // --- Khởi động trên thread nền (xem "luồng khởi động" ở ShareWindow.h) ---
    AgentOptions opt;
    opt.fps = 60;
    opt.bitrateMbps = 20;
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
                gtk_label_set_markup(GTK_LABEL(headline_),
                    "<b>Could not start sharing.</b>");
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
        gtk_label_set_markup(GTK_LABEL(headline_), "<b>Sharing stopped.</b>");
        gtk_widget_destroy(window_);
        return;
    }

    const std::vector<AgentSourceStatus> rows = agent_.Status();

    char head[128];
    std::snprintf(head, sizeof(head), "<b>Sharing %zu screen(s) on UDP port %u</b>", rows.size(),
        unsigned(kDeskhubPort));
    gtk_label_set_markup(GTK_LABEL(headline_), head);

    // Dựng lại toàn bộ danh sách mỗi lượt. Với vài hàng thì đây là cách đơn giản
    // nhất và chi phí không đáng kể — GTK dựng lại một GtkLabel rẻ hơn nhiều so
    // với chi phí giữ đồng bộ hai danh sách.
    GList* kids = gtk_container_get_children(GTK_CONTAINER(rowsBox_));
    for (GList* l = kids; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    for (const AgentSourceStatus& r : rows) {
        char text[512];
        if (r.viewerConnected)
            std::snprintf(text, sizeof(text),
                "%s — %ux%u\n    viewer %s · %.0f fps sent · %.0f kbps · RTT %u ms · capture "
                "%.0f fps%s",
                r.name.c_str(), r.width, r.height, r.viewerAddr.c_str(), r.sendFps, r.sendKbps,
                r.rttMs, r.captureFps, r.zeroCopy ? " · zero-copy" : " · CPU copy");
        else
            std::snprintf(text, sizeof(text),
                "%s — %ux%u\n    waiting for a viewer · capture "
                "%.0f fps%s",
                r.name.c_str(), r.width, r.height, r.captureFps,
                r.zeroCopy ? " · zero-copy" : " · CPU copy");

        GtkWidget* label = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.f);
        gtk_box_pack_start(GTK_BOX(rowsBox_), label, FALSE, FALSE, 0);
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
    delete self;
}
