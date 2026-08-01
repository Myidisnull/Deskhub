#include "gtk/MainWindow.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "deskhubp/media/DisplayEnum.h"
#include "gtk/GtkUtil.h"
#include "gtk/ShareWindow.h"
#include "gtk/ViewerWindow.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/SourceQuery.h"
#include "deskhubp/net/UdpSocket.h"

#include "deskhub/media/QualityPreset.h"
#include "deskhub/session/ConnectFlow.h"
#include "deskhub/ui/Strings.h"
#include "deskhub/media/SourceLabel.h"
#include "deskhub/protocol/Wire.h"

namespace {

constexpr AgentOptions kShareDefaults{};

constexpr int kWinW = 496;

std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

uint32_t EntryUint(GtkWidget* entry, uint32_t fallback) {
    const int v = std::atoi(gtk_entry_get_text(GTK_ENTRY(entry)));
    return v > 0 ? uint32_t(v) : fallback;
}

uint32_t ComboMaxDim(GtkWidget* combo, uint32_t fallback) {
    const gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    if (active < 0 || size_t(active) >= deskhub::media::kQualityPresets.size()) return fallback;
    return deskhub::media::kQualityPresets[size_t(active)].maxDim;
}

GtkWidget* Label(const char* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0.f);
    return l;
}

GtkWidget* NumberEntry(const char* value) {
    GtkWidget* e = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e), value);
    gtk_entry_set_width_chars(GTK_ENTRY(e), 4);
    gtk_entry_set_max_length(GTK_ENTRY(e), 5);
    return e;
}

bool PickSources(GtkWindow* parent, const std::vector<deskhub::SourceInfo>& sources,
    std::vector<deskhub::SourceInfo>& out) {
    out.clear();
    if (sources.empty()) return false;
    if (!deskhub::DecideAfterSourceQuery(sources).showPicker) {
        out = sources;
        return true;
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons(deskhub::ui::kPickerTitle, parent,
        GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_View", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 460, 340);

    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_box_set_spacing(GTK_BOX(box), 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkListStore* store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    for (size_t i = 0; i < sources.size(); ++i) {
        const std::string line = deskhub::media::SourcePickerLabel(sources[i].name,
            sources[i].sourceId, sources[i].width, sources[i].height);
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, line.c_str(), 1, int(i), -1);
    }

    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), FALSE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(nullptr, gtk_cell_renderer_text_new(), "text", 0,
            nullptr));

    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
    gtk_tree_selection_select_all(sel);

    const auto onActivate = +[](GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer d) {
        gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
    };
    g_signal_connect(tree, "row-activated", G_CALLBACK(onActivate), dlg);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(box), Label(deskhub::ui::kPickerEachWindow), FALSE,
        FALSE, 0);
    gtk_widget_show_all(dlg);

    for (;;) {
        if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_ACCEPT) break;

        GList* rows = gtk_tree_selection_get_selected_rows(sel, nullptr);
        for (GList* l = rows; l; l = l->next) {
            GtkTreeIter it;
            if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &it,
                    static_cast<GtkTreePath*>(l->data)))
                continue;
            int idx = -1;
            gtk_tree_model_get(GTK_TREE_MODEL(store), &it, 1, &idx, -1);
            if (idx >= 0 && size_t(idx) < sources.size()) out.push_back(sources[size_t(idx)]);
        }
        g_list_free_full(rows, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));
        if (!out.empty()) break;
    }

    gtk_widget_destroy(dlg);
    return !out.empty();
}

}

void MainWindow::Open(GtkApplication* app) {
    auto* w = new MainWindow();
    w->Build(app);
}

void MainWindow::Build(GtkApplication* app) {
    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_), deskhub::ui::kAppTitle);
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
    gtk_widget_set_size_request(window_, kWinW, -1);
    g_signal_connect(window_, "destroy", G_CALLBACK(OnDestroy), this);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(window_), box);

    gtk_box_pack_start(GTK_BOX(box), BuildHostBox(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), BuildClientBox(), FALSE, FALSE, 0);

    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* exitButton = gtk_button_new_with_label("Exit");
    gtk_widget_set_size_request(exitButton, 100, 28);
    g_signal_connect(exitButton, "clicked", G_CALLBACK(OnExitClicked), this);
    gtk_box_pack_start(GTK_BOX(bottom), exitButton, FALSE, FALSE, 0);

    statusLabel_ = Label("");
    gtk_label_set_ellipsize(GTK_LABEL(statusLabel_), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(bottom), statusLabel_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), bottom, FALSE, FALSE, 0);

    gtk_window_set_default(GTK_WINDOW(window_), connectButton_);

    gtk_widget_show_all(window_);
}

GtkWidget* MainWindow::BuildHostBox() {
    GtkWidget* frame = gtk_frame_new("Host mode - share an application on THIS machine");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(frame), box);

    gtk_box_pack_start(GTK_BOX(box), Label(deskhub::ui::kHostIpIntro),
        FALSE, FALSE, 0);

    const auto addrs = ListLocalIPv4();
    if (addrs.empty()) {
        gtk_box_pack_start(GTK_BOX(box), Label(deskhub::ui::kNoNetworkAddress), FALSE, FALSE,
            0);
    } else {
        GtkWidget* grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
        int row = 0;
        for (const auto& a : addrs) {
            GtkWidget* line = Label((a.name + "    " + a.ip).c_str());
            gtk_label_set_ellipsize(GTK_LABEL(line), PANGO_ELLIPSIZE_END);
            gtk_label_set_selectable(GTK_LABEL(line), TRUE);
            gtk_widget_set_hexpand(line, TRUE);
            gtk_grid_attach(GTK_GRID(grid), line, 0, row, 1, 1);

            GtkWidget* copy = gtk_button_new_with_label("Copy");
            gtk_widget_set_size_request(copy, 60, 20);
            g_object_set_data_full(G_OBJECT(copy), "deskhub-ip", g_strdup(a.ip.c_str()), g_free);
            g_signal_connect(copy, "clicked", G_CALLBACK(OnCopyClicked), this);
            gtk_grid_attach(GTK_GRID(grid), copy, 1, row, 1, 1);
            ++row;
        }
        gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(box), Label(deskhub::ui::UdpPortLine().c_str()), FALSE, FALSE,
        0);

    GtkWidget* settings = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(settings), Label("FPS"), FALSE, FALSE, 0);
    fpsEntry_ = NumberEntry("60");
    gtk_box_pack_start(GTK_BOX(settings), fpsEntry_, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(settings), Label("Bitrate (Mbps)"), FALSE, FALSE, 0);
    bitrateEntry_ = NumberEntry("20");
    gtk_box_pack_start(GTK_BOX(settings), bitrateEntry_, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(settings), Label("Quality"), FALSE, FALSE, 0);
    qualityCombo_ = gtk_combo_box_text_new();
    for (const auto& preset : deskhub::media::kQualityPresets)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(qualityCombo_), preset.label);
    gtk_combo_box_set_active(GTK_COMBO_BOX(qualityCombo_),
        gint(deskhub::media::QualityPresetIndex(kShareDefaults.maxDim)));
    gtk_box_pack_start(GTK_BOX(settings), qualityCombo_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), settings, FALSE, FALSE, 0);

    shareButton_ = gtk_button_new_with_label("Share...  (pick the display to share)");
    gtk_widget_set_size_request(shareButton_, -1, 32);
    g_signal_connect(shareButton_, "clicked", G_CALLBACK(OnShareClicked), this);
    gtk_box_pack_start(GTK_BOX(box), shareButton_, FALSE, FALSE, 0);

    return frame;
}

GtkWidget* MainWindow::BuildClientBox() {
    GtkWidget* frame = gtk_frame_new("Client mode - connect to ANOTHER machine");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(frame), box);

    gtk_box_pack_start(GTK_BOX(box), Label(deskhub::ui::kClientIpPrompt), FALSE, FALSE, 0);

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addressEntry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(addressEntry_), "192.168.1.10");
    g_signal_connect(addressEntry_, "activate", G_CALLBACK(OnAddressActivate), this);
    gtk_box_pack_start(GTK_BOX(row), addressEntry_, TRUE, TRUE, 0);

    connectButton_ = gtk_button_new_with_label("Connect");
    gtk_widget_set_size_request(connectButton_, 100, -1);
    gtk_widget_set_can_default(connectButton_, TRUE);
    g_signal_connect(connectButton_, "clicked", G_CALLBACK(OnConnectClicked), this);
    gtk_box_pack_start(GTK_BOX(row), connectButton_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    return frame;
}

void MainWindow::SetBusy(bool busy, const char* what) {
    gtk_widget_set_sensitive(shareButton_, !busy);
    gtk_widget_set_sensitive(connectButton_, !busy);
    gtk_label_set_text(GTK_LABEL(statusLabel_), busy && what ? what : "");
}

void MainWindow::HideForSession() {
    gtk_widget_hide(window_);
}

void MainWindow::ShowAfterSession() {
    gtk_widget_show(window_);
    gtk_window_present(GTK_WINDOW(window_));
}

void MainWindow::OnCopyClicked(GtkButton* b, gpointer) {
    const char* ip = static_cast<const char*>(g_object_get_data(G_OBJECT(b), "deskhub-ip"));
    if (!ip) return;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), ip, -1);
}

void MainWindow::OnExitClicked(GtkButton*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    gtk_widget_destroy(self->window_);
}

void MainWindow::OnShareClicked(GtkButton*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    AgentOptions opt;
    opt.fps = EntryUint(self->fpsEntry_, kShareDefaults.fps);
    opt.bitrateMbps = EntryUint(self->bitrateEntry_, kShareDefaults.bitrateMbps);
    opt.maxDim = ComboMaxDim(self->qualityCombo_, kShareDefaults.maxDim);

    self->SetBusy(true, "Waiting for the screen-sharing dialog…");

    std::thread([self, opt, alive = self->alive_] {
        std::vector<AgentSource> sources = deskhubp::ListDisplays();
        const std::string err = sources.empty() ? deskhubp::ListDisplaysError() : std::string();

        RunOnMain([self, sources, opt, err, alive]() mutable {
            if (!alive->load()) return;
            self->SetBusy(false, nullptr);
            if (sources.empty()) {
                if (!err.empty() && err != "cancelled by the user")
                    ShowError(GTK_WINDOW(self->window_), "Screen capture is not available", err);
                return;
            }

            if (sources.size() > deskhub::kMaxSources) {
                char msg[192];
                std::snprintf(msg, sizeof(msg),
                    "More than %zu screens were picked. Only the first %zu will be shared.",
                    deskhub::kMaxSources, deskhub::kMaxSources);
                ShowWarning(GTK_WINDOW(self->window_), "Deskhub", msg);
                sources.resize(deskhub::kMaxSources);
            }

            self->HideForSession();
            ShareWindow::Open(sources, opt, [self, alive] {
                if (!alive->load()) return;
                self->ShowAfterSession();
            });
        });
    }).detach();
}

void MainWindow::OnAddressActivate(GtkEntry*, gpointer user) {
    OnConnectClicked(nullptr, user);
}

void MainWindow::OnConnectClicked(GtkButton*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);

    const std::string text = Trim(gtk_entry_get_text(GTK_ENTRY(self->addressEntry_)));
    if (text.empty()) {
        ShowWarning(GTK_WINDOW(self->window_), "Deskhub",
            "Enter the host machine's IP address first (e.g., 192.168.1.10).");
        return;
    }

    NetAddr server{};
    if (!ParseNetAddr(text, server)) {
        ShowError(GTK_WINDOW(self->window_), ("Invalid address: \"" + text + "\"").c_str(),
            deskhub::ui::InvalidAddressHint().c_str());
        return;
    }

    self->SetBusy(true, deskhub::ui::kQueryingSources);

    std::thread([self, server, alive = self->alive_] {
        std::vector<deskhub::SourceInfo> sources;
        const bool ok = QuerySources(server, sources);

        RunOnMain([self, server, sources, ok, alive] {
            if (!alive->load()) return;
            self->SetBusy(false, nullptr);

            std::vector<deskhub::SourceInfo> picked;
            if (ok && !sources.empty()) {
                if (!PickSources(GTK_WINDOW(self->window_), sources, picked)) return;
            } else {
                picked.push_back(deskhub::SourceInfo{});
            }

            self->HideForSession();
            int opened = 0;
            for (const auto& s : picked) {
                if (ViewerWindow::Open(server, s.sourceId, s.name, [self, alive] {
                        if (!alive->load()) return;
                        if (--self->openViewers_ <= 0) self->ShowAfterSession();
                    })) {
                    ++self->openViewers_;
                    ++opened;
                }
            }
            if (opened == 0) {
                self->ShowAfterSession();
                ShowWarning(GTK_WINDOW(self->window_), "Deskhub",
                    deskhub::ui::kViewerOpenFailed);
            }
        });
    }).detach();
}

void MainWindow::OnDestroy(GtkWidget*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    self->alive_->store(false);
    delete self;
}
