#include "gtk/ShareWindow.h"

#include <cstdio>
#include <utility>

#include "deskhub/media/ShareStatusText.h"
#include "deskhub/media/SourceLabel.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/media/DisplayEnum.h"
#include "gtk/GtkUtil.h"
#include "deskhubp/net/UdpSocket.h"

namespace {

constexpr int kWinW = 460;
constexpr int kWinH = 330;

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

GtkWidget* MakeRow(const char* text, const char* tooltip) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_end(label, 6);
    if (tooltip) gtk_widget_set_tooltip_text(label, tooltip);
    return label;
}

}

void ShareWindow::Open(const std::vector<AgentSource>& sources, const AgentOptions& opt,
    std::function<void()> onClosed) {
    auto* s = new ShareWindow();
    s->onClosed_ = std::move(onClosed);
    s->Build(sources, opt);
}

ShareWindow::~ShareWindow() {
    if (timer_) g_source_remove(timer_);
    driver_.Join();
    agent_.Stop();
    deskhubp::ReleaseDisplays();
}

void ShareWindow::Build(const std::vector<AgentSource>& sources, const AgentOptions& optIn) {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), deskhub::ui::kSharingTitle);
    gtk_window_set_default_size(GTK_WINDOW(window_), kWinW, kWinH);
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
    PlaceBottomRight(GTK_WINDOW(window_));
    g_signal_connect(window_, "destroy", G_CALLBACK(OnDestroy), this);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(window_), box);

    GtkWidget* caption = gtk_label_new(deskhub::ui::kSharingSourcesIntro);
    gtk_label_set_xalign(GTK_LABEL(caption), 0.f);
    gtk_box_pack_start(GTK_BOX(box), caption, FALSE, FALSE, 0);

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
    gtk_container_add(GTK_CONTAINER(rowsBox_), MakeRow("(starting…)", nullptr));

    GtkWidget* hint = gtk_label_new(deskhub::ui::kSharingConnectHint);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.f);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    GtkWidget* buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget* stop = gtk_button_new_with_label(deskhub::ui::kStopSharing);
    gtk_widget_set_size_request(stop, 130, 28);
    g_signal_connect(stop, "clicked", G_CALLBACK(OnStopClicked), this);
    gtk_box_pack_start(GTK_BOX(buttons), stop, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    gtk_widget_show_all(window_);

    AgentOptions opt = optIn;
    DesktopBounds(opt.desktopX, opt.desktopY, opt.desktopW, opt.desktopH);

    driver_.StartAsync(
        agent_, sources, opt,
        [alive = alive_](std::function<void()> fn) {
            RunOnMain([alive, fn = std::move(fn)] {
                if (alive->load()) fn();
            });
        },
        [this](bool started, const std::string& error) {
            if (!started) {
                ShowError(GTK_WINDOW(window_), deskhub::ui::kShareStartFailed, error);
                gtk_widget_destroy(window_);
                return;
            }
            Refresh();
        });

    timer_ = g_timeout_add(deskhubp::kAgentStatusPollMs, OnTimer, this);
}

gboolean ShareWindow::OnTimer(gpointer user) {
    static_cast<ShareWindow*>(user)->Refresh();
    return G_SOURCE_CONTINUE;
}

void ShareWindow::Refresh() {
    if (!window_) return;

    std::vector<AgentSourceStatus> rows;
    switch (driver_.Poll(agent_, rows)) {
        case deskhubp::AgentDriveState::Starting: return;
        case deskhubp::AgentDriveState::Stopped: gtk_widget_destroy(window_); return;
        case deskhubp::AgentDriveState::Running: RefreshList(rows); return;
    }
}

void ShareWindow::RefreshList(const std::vector<AgentSourceStatus>& rows) {
    GList* kids = gtk_container_get_children(GTK_CONTAINER(rowsBox_));
    for (GList* l = kids; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    if (rows.empty()) {
        gtk_container_add(GTK_CONTAINER(rowsBox_),
            MakeRow(deskhub::ui::kNothingShared, nullptr));
        gtk_widget_show_all(rowsBox_);
        return;
    }

    for (const AgentSourceStatus& r : rows) {
        const std::string text =
            deskhub::media::SharedSourceLabel(r.name, r.width, r.height, r.viewerConnected);
        const std::string tip = deskhub::media::ShareStatusTooltip(r);
        gtk_container_add(GTK_CONTAINER(rowsBox_), MakeRow(text.c_str(), tip.c_str()));
    }
    gtk_widget_show_all(rowsBox_);
}

void ShareWindow::OnStopClicked(GtkButton*, gpointer user) {
    auto* self = static_cast<ShareWindow*>(user);
    gtk_widget_destroy(self->window_);
}

void ShareWindow::OnDestroy(GtkWidget*, gpointer user) {
    auto* self = static_cast<ShareWindow*>(user);
    self->alive_->store(false);
    self->window_ = nullptr;
    if (self->timer_) {
        g_source_remove(self->timer_);
        self->timer_ = 0;
    }
    auto done = std::move(self->onClosed_);
    delete self;
    if (done) done();
}
