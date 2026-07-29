// =============================================================================
// MainWindow.cpp — màn hình chính: danh sách IP + Share + Connect.
//
// LIÊN QUAN: gtk/MainWindow.h (⚠ vì sao cả hai nút chạy trên thread nền)
// =============================================================================
#include "gtk/MainWindow.h"

#include <string>
#include <thread>
#include <vector>

#include "Log.h"
#include "capture/SourceEnum.h"
#include "gtk/GtkUtil.h"
#include "gtk/ShareWindow.h"
#include "gtk/ViewerWindow.h"
#include "net/NetInfo.h"
#include "net/SourceQuery.h"
#include "net/UdpSocket.h"

namespace {

// Hộp thoại chọn nguồn khi host đang chia sẻ NHIỀU màn hình. Trả về chỉ số đã
// chọn, hoặc -1 nếu người dùng huỷ. Một nguồn thì không hỏi — người dùng bấm
// Connect là muốn xem, không phải muốn trả lời thêm một câu hỏi.
int PickSource(GtkWindow* parent, const std::vector<deskhub::SourceInfo>& sources) {
    if (sources.size() <= 1) return sources.empty() ? 0 : 0;

    GtkWidget* dlg = gtk_dialog_new_with_buttons("Which screen?", parent, GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL, "_View", GTK_RESPONSE_ACCEPT, nullptr);
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkWidget* combo = gtk_combo_box_text_new();
    for (const auto& s : sources) {
        char text[256];
        std::snprintf(text, sizeof(text), "%s (%ux%u)", s.name.c_str(), s.width, s.height);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), text);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_container_add(GTK_CONTAINER(box), combo);
    gtk_widget_show_all(dlg);

    const int resp = gtk_dialog_run(GTK_DIALOG(dlg));
    const int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    gtk_widget_destroy(dlg);
    return resp == GTK_RESPONSE_ACCEPT ? idx : -1;
}

} // namespace

void MainWindow::Open(GtkApplication* app) {
    auto* w = new MainWindow();
    w->Build(app);
}

void MainWindow::Build(GtkApplication* app) {
    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_), "Deskhub");
    gtk_window_set_default_size(GTK_WINDOW(window_), 480, 360);
    g_signal_connect(window_, "destroy", G_CALLBACK(OnDestroy), this);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 20);
    gtk_container_add(GTK_CONTAINER(window_), box);

    GtkWidget* title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size=\"x-large\"><b>Deskhub</b></span>\n"
        "Share this machine's screen, or connect to another one.");
    gtk_label_set_xalign(GTK_LABEL(title), 0.f);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    // --- Địa chỉ của máy này ---
    GtkWidget* addrs = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(addrs), 0.f);
    gtk_label_set_selectable(GTK_LABEL(addrs), TRUE);
    std::string text = "This machine:\n";
    const auto list = ListLocalIPv4();
    if (list.empty())
        text += "    (no network interface is up)\n";
    else
        for (const auto& a : list) text += "    " + a.ip + "    (" + a.name + ")\n";
    gtk_label_set_text(GTK_LABEL(addrs), text.c_str());
    gtk_box_pack_start(GTK_BOX(box), addrs, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE,
        0);

    // --- Share ---
    shareButton_ = gtk_button_new_with_label("Share this screen");
    g_signal_connect(shareButton_, "clicked", G_CALLBACK(OnShareClicked), this);
    gtk_box_pack_start(GTK_BOX(box), shareButton_, FALSE, FALSE, 0);

    // --- Connect ---
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addressEntry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(addressEntry_), "192.168.1.20");
    // Enter trong ô địa chỉ = bấm Connect. Người dùng gõ IP xong thì phản xạ là
    // bấm Enter, không phải rê chuột sang nút.
    g_signal_connect(addressEntry_, "activate", G_CALLBACK(OnAddressActivate), this);
    gtk_box_pack_start(GTK_BOX(row), addressEntry_, TRUE, TRUE, 0);

    connectButton_ = gtk_button_new_with_label("Connect");
    g_signal_connect(connectButton_, "clicked", G_CALLBACK(OnConnectClicked), this);
    gtk_box_pack_start(GTK_BOX(row), connectButton_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    statusLabel_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(statusLabel_), 0.f);
    gtk_box_pack_start(GTK_BOX(box), statusLabel_, FALSE, FALSE, 0);

    GtkWidget* hint = gtk_label_new(
        "Port is always UDP 47777. Whoever connects gets mouse and keyboard.\n"
        "Over the Internet: run Tailscale on both machines and use the 100.x address.");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.f);
    gtk_widget_set_sensitive(hint, FALSE);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    gtk_widget_show_all(window_);
}

// Khoá hai nút trong lúc một thao tác nền đang chạy: bấm Share hai lần sẽ mở hai
// phiên portal chồng nhau, và bấm Connect trong lúc đang hỏi nguồn sẽ mở hai
// socket dò cùng lúc.
void MainWindow::SetBusy(bool busy, const char* what) {
    gtk_widget_set_sensitive(shareButton_, !busy);
    gtk_widget_set_sensitive(connectButton_, !busy);
    gtk_label_set_text(GTK_LABEL(statusLabel_), busy ? what : "");
}

void MainWindow::OnShareClicked(GtkButton*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    self->SetBusy(true, "Waiting for the screen-sharing dialog…");

    std::thread([self, alive = self->alive_] {
        // CHẶN: hộp thoại hệ thống của portal. Đây chính là lý do phải ở thread nền.
        std::vector<ShareSource> sources = GetShareSources();
        const std::string err = sources.empty() ? ShareSourceError() : std::string();

        RunOnMain([self, sources, err, alive] {
            if (!alive->load()) return;
            self->SetBusy(false, nullptr);
            if (sources.empty()) {
                // Người dùng bấm huỷ thì không phải lỗi — im lặng quay lại.
                if (!err.empty() && err != "cancelled by the user")
                    ShowError(GTK_WINDOW(self->window_), "Screen capture is not available", err);
                return;
            }
            ShareWindow::Open(sources);
        });
    }).detach();
}

void MainWindow::OnAddressActivate(GtkEntry*, gpointer user) {
    OnConnectClicked(nullptr, user);
}

void MainWindow::OnConnectClicked(GtkButton*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);

    const std::string text = gtk_entry_get_text(GTK_ENTRY(self->addressEntry_));
    NetAddr server{};
    if (!ParseNetAddr(text, server)) {
        ShowError(GTK_WINDOW(self->window_), "That is not a valid address",
            "Type the other machine's IPv4 address, without a port — for example "
            "192.168.1.20. The port is always UDP 47777.");
        return;
    }

    self->SetBusy(true, "Asking the other machine what it is sharing…");

    std::thread([self, server, alive = self->alive_] {
        // CHẶN tới 3 giây (net/SourceQuery.h).
        std::vector<deskhub::SourceInfo> sources;
        const bool ok = QuerySources(server, sources);

        RunOnMain([self, server, sources, ok, alive] {
            if (!alive->load()) return;
            self->SetBusy(false, nullptr);

            // Host im lặng KHÔNG phải lỗi tử vong: bản cũ không biết LIST_SOURCES.
            // Cứ thử nguồn 0 — nếu máy kia thật sự không có ai nghe thì phiên sẽ
            // tự chết vì timeout và ViewerWindow báo lý do.
            uint8_t sourceId = 0;
            std::string title = "Deskhub — " + server.ToString();
            if (ok && !sources.empty()) {
                const int idx = PickSource(GTK_WINDOW(self->window_), sources);
                if (idx < 0) return; // người dùng huỷ
                sourceId = sources[size_t(idx)].sourceId;
                title = "Deskhub — " + sources[size_t(idx)].name + " @ " + server.ToString();
            }
            if (!ViewerWindow::Open(server, sourceId, title))
                ShowError(GTK_WINDOW(self->window_), "Could not start the session",
                    "Failed to open a UDP socket.");
        });
    }).detach();
}

void MainWindow::OnDestroy(GtkWidget*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    self->alive_->store(false);
    delete self;
}
