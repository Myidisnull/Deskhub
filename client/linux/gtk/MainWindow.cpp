// =============================================================================
// MainWindow.cpp — màn hình chính: hộp Host mode + hộp Client mode.
//
// Bố cục và câu chữ chép từ client/windows/win32/MainMenuWindow.cpp; chỗ nào lệch
// đều có chú thích tại chỗ. Khác biệt CẤU TRÚC duy nhất: bên Windows DoShare/
// DoConnect CHẶN suốt phiên, ở đây không chặn được nên dùng callback — xem
// gtk/MainWindow.h.
//
// LIÊN QUAN: gtk/MainWindow.h (⚠ vì sao cả hai nút chạy trên thread nền)
// =============================================================================
#include "gtk/MainWindow.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "capture/SourceEnum.h"
#include "gtk/GtkUtil.h"
#include "gtk/ShareWindow.h"
#include "gtk/ViewerWindow.h"
#include "net/NetInfo.h"
#include "net/SourceQuery.h"
#include "net/UdpSocket.h"

#include "deskhub/protocol/Wire.h" // kMaxSources

namespace {

constexpr uint32_t kDefaultFps = 60;
constexpr uint32_t kDefaultBitrateMbps = 20;

// Bề rộng cửa sổ, chép từ kW của MainMenuWindow.cpp.
constexpr int kWinW = 496;

std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Số dương trong ô, hoặc `fallback` nếu ô rỗng/rác. Đối ứng GetEditUint bên Win32.
uint32_t EntryUint(GtkWidget* entry, uint32_t fallback) {
    const int v = std::atoi(gtk_entry_get_text(GTK_ENTRY(entry)));
    return v > 0 ? uint32_t(v) : fallback;
}

// Nhãn canh trái — thứ ta dùng ở mọi nơi, gom lại cho khỏi lặp bốn dòng mỗi lần.
GtkWidget* Label(const char* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0.f);
    return l;
}

// Ô số nhỏ (FPS, Bitrate).
GtkWidget* NumberEntry(const char* value) {
    GtkWidget* e = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e), value);
    gtk_entry_set_width_chars(GTK_ENTRY(e), 4);
    gtk_entry_set_max_length(GTK_ENTRY(e), 5);
    return e;
}

// Hộp thoại chọn nguồn phía client. Đối ứng SourcePickerDialog.cpp, KỂ CẢ việc
// chọn NHIỀU nguồn: mỗi nguồn được chọn sẽ mở một cửa sổ xem riêng.
// Trả false nếu người dùng huỷ.
bool PickSources(GtkWindow* parent, const std::vector<deskhub::SourceInfo>& sources,
    std::vector<deskhub::SourceInfo>& out) {
    out.clear();
    if (sources.empty()) return false;
    // Host chỉ chia sẻ một thứ: không bắt người dùng bấm thêm một hộp thoại nữa.
    if (sources.size() == 1) {
        out = sources;
        return true;
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons("What do you want to view?", parent,
        GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_View", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 460, 340);

    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_box_set_spacing(GTK_BOX(box), 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkListStore* store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    for (size_t i = 0; i < sources.size(); ++i) {
        char line[288];
        std::snprintf(line, sizeof(line), "%s (%ux%u)", sources[i].name.c_str(),
            sources[i].width, sources[i].height);
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, line, 1, int(i), -1);
    }

    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), FALSE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(nullptr, gtk_cell_renderer_text_new(), "text", 0,
            nullptr));

    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
    gtk_tree_selection_select_all(sel); // mặc định chọn HẾT, như LB_SETSEL bên Win32

    // Nháy đúp một dòng = chọn đúng dòng đó rồi bấm View (đối ứng LBN_DBLCLK).
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

    gtk_box_pack_start(GTK_BOX(box), Label("Each one you pick opens its own window."), FALSE,
        FALSE, 0);
    gtk_widget_show_all(dlg);

    // Vòng lặp, không phải một lần chạy: bấm View mà chưa chọn gì thì hộp thoại
    // đứng yên chờ tiếp, giống Confirm() bên Win32 (nó `return` chứ không đóng).
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

} // namespace

void MainWindow::Open(GtkApplication* app) {
    auto* w = new MainWindow();
    w->Build(app);
}

void MainWindow::Build(GtkApplication* app) {
    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_),
        "Deskhub - stream & remotely control an application");
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
    gtk_widget_set_size_request(window_, kWinW, -1);
    g_signal_connect(window_, "destroy", G_CALLBACK(OnDestroy), this);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(window_), box);

    gtk_box_pack_start(GTK_BOX(box), BuildHostBox(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), BuildClientBox(), FALSE, FALSE, 0);

    // Hàng cuối: Exit bên trái (như bản Win32), dòng trạng thái bên phải. Bản Win32
    // KHÔNG có dòng trạng thái vì nó chặn cả cửa sổ trong lúc làm việc; ở đây hai
    // thao tác chạy nền nên phải nói ra máy đang chờ cái gì.
    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* exitButton = gtk_button_new_with_label("Exit");
    gtk_widget_set_size_request(exitButton, 100, 28);
    g_signal_connect(exitButton, "clicked", G_CALLBACK(OnExitClicked), this);
    gtk_box_pack_start(GTK_BOX(bottom), exitButton, FALSE, FALSE, 0);

    statusLabel_ = Label("");
    gtk_label_set_ellipsize(GTK_LABEL(statusLabel_), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(bottom), statusLabel_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), bottom, FALSE, FALSE, 0);

    // Connect là nút mặc định (BS_DEFPUSHBUTTON bên Win32): Enter ở bất kỳ ô nào
    // cũng là "kết nối", vì đó là việc chính của màn hình này.
    gtk_window_set_default(GTK_WINDOW(window_), connectButton_);

    gtk_widget_show_all(window_);
}

// --- Hộp HOST ---------------------------------------------------------------
GtkWidget* MainWindow::BuildHostBox() {
    GtkWidget* frame = gtk_frame_new("Host mode - share an application on THIS machine");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(frame), box);

    gtk_box_pack_start(GTK_BOX(box), Label("Others connect to you using one of these IP addresses:"),
        FALSE, FALSE, 0);

    const auto addrs = ListLocalIPv4();
    if (addrs.empty()) {
        gtk_box_pack_start(GTK_BOX(box), Label("(no network address found)"), FALSE, FALSE, 0);
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

            // Giá trị Copy là IP TRẦN — đúng thứ phải gõ vào ô địa chỉ phía kia.
            // Dán vào chính nút, để callback khỏi phải tra ngược danh sách.
            GtkWidget* copy = gtk_button_new_with_label("Copy");
            gtk_widget_set_size_request(copy, 60, 20);
            g_object_set_data_full(G_OBJECT(copy), "deskhub-ip", g_strdup(a.ip.c_str()), g_free);
            g_signal_connect(copy, "clicked", G_CALLBACK(OnCopyClicked), this);
            gtk_grid_attach(GTK_GRID(grid), copy, 1, row, 1, 1);
            ++row;
        }
        gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
    }

    // Không có ô Port: cổng là hằng số kDeskhubPort (net/UdpSocket.h). Chỉ nói ra
    // cho người dùng biết con số đó, vì firewall doanh nghiệp có thể cần mở tay.
    GtkWidget* settings = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    char portText[32];
    std::snprintf(portText, sizeof(portText), "UDP port %u", unsigned(kDeskhubPort));
    GtkWidget* portLabel = Label(portText);
    gtk_widget_set_hexpand(portLabel, TRUE);
    gtk_box_pack_start(GTK_BOX(settings), portLabel, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(settings), Label("FPS"), FALSE, FALSE, 0);
    fpsEntry_ = NumberEntry("60");
    gtk_box_pack_start(GTK_BOX(settings), fpsEntry_, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(settings), Label("Bitrate (Mbps)"), FALSE, FALSE, 0);
    bitrateEntry_ = NumberEntry("20");
    gtk_box_pack_start(GTK_BOX(settings), bitrateEntry_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), settings, FALSE, FALSE, 0);

    shareButton_ = gtk_button_new_with_label("Share...  (pick the display to share)");
    gtk_widget_set_size_request(shareButton_, -1, 32);
    g_signal_connect(shareButton_, "clicked", G_CALLBACK(OnShareClicked), this);
    gtk_box_pack_start(GTK_BOX(box), shareButton_, FALSE, FALSE, 0);

    return frame;
}

// --- Hộp CLIENT -------------------------------------------------------------
GtkWidget* MainWindow::BuildClientBox() {
    GtkWidget* frame = gtk_frame_new("Client mode - connect to ANOTHER machine");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(frame), box);

    gtk_box_pack_start(GTK_BOX(box), Label("Host machine IP address:"), FALSE, FALSE, 0);

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addressEntry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(addressEntry_), "192.168.1.10");
    // Enter trong ô địa chỉ = bấm Connect. Người dùng gõ IP xong thì phản xạ là
    // bấm Enter, không phải rê chuột sang nút.
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

// ---------------------------------------------------------------------------
// Trạng thái + ẩn/hiện quanh phiên
// ---------------------------------------------------------------------------

// Khoá hai nút trong lúc một thao tác nền đang chạy: bấm Share hai lần sẽ mở hai
// phiên portal chồng nhau, và bấm Connect trong lúc đang hỏi nguồn sẽ mở hai
// socket dò cùng lúc.
void MainWindow::SetBusy(bool busy, const char* what) {
    gtk_widget_set_sensitive(shareButton_, !busy);
    gtk_widget_set_sensitive(connectButton_, !busy);
    gtk_label_set_text(GTK_LABEL(statusLabel_), busy && what ? what : "");
}

void MainWindow::HideForSession() {
    gtk_widget_hide(window_);
}

// GtkApplication vẫn giữ tiến trình sống trong lúc cửa sổ này ẩn (nó đếm cửa sổ đã
// ĐĂNG KÝ, không phải cửa sổ đang hiện), nên không cần giữ thêm tham chiếu nào.
void MainWindow::ShowAfterSession() {
    gtk_widget_show(window_);
    gtk_window_present(GTK_WINDOW(window_));
}

// ---------------------------------------------------------------------------
// Nút
// ---------------------------------------------------------------------------
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
    // Đọc hai ô TRƯỚC khi ẩn cửa sổ — sau đó widget vẫn sống, nhưng lấy ở đây thì
    // giá trị chắc chắn là thứ người dùng đang nhìn thấy lúc bấm.
    AgentOptions opt;
    opt.fps = EntryUint(self->fpsEntry_, kDefaultFps);
    opt.bitrateMbps = EntryUint(self->bitrateEntry_, kDefaultBitrateMbps);

    self->SetBusy(true, "Waiting for the screen-sharing dialog…");

    std::thread([self, opt, alive = self->alive_] {
        // CHẶN: hộp thoại hệ thống của portal. Đây chính là lý do phải ở thread nền.
        std::vector<ShareSource> sources = GetShareSources();
        const std::string err = sources.empty() ? ShareSourceError() : std::string();

        RunOnMain([self, sources, opt, err, alive]() mutable {
            if (!alive->load()) return;
            self->SetBusy(false, nullptr);
            if (sources.empty()) {
                // Người dùng bấm huỷ thì không phải lỗi — im lặng quay lại.
                if (!err.empty() && err != "cancelled by the user")
                    ShowError(GTK_WINDOW(self->window_), "Screen capture is not available", err);
                return;
            }

            // kMaxSources là trần của SOURCE_LIST trong MỘT datagram, không phải một
            // con số tuỳ ý — chọn nhiều hơn thế thì cắt bớt và NÓI RA (giống DoShare
            // bên Windows).
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
        // Nói rõ cả cổng, vì đường sai hay gặp nhất là người dùng gõ kèm ":port"
        // theo thói quen — ParseNetAddr từ chối chuỗi đó chứ không lờ đi.
        char msg[256];
        std::snprintf(msg, sizeof(msg),
            "Enter just the IP address (e.g., 192.168.1.10). Deskhub always uses UDP port %u.",
            unsigned(kDeskhubPort));
        ShowError(GTK_WINDOW(self->window_), ("Invalid address: \"" + text + "\"").c_str(), msg);
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
            // tự chết vì timeout và cửa sổ xem báo lý do.
            std::vector<deskhub::SourceInfo> picked;
            if (ok && !sources.empty()) {
                if (!PickSources(GTK_WINDOW(self->window_), sources, picked)) return; // huỷ
            } else {
                picked.push_back(deskhub::SourceInfo{}); // sourceId 0, tên rỗng
            }

            // Ẩn TRƯỚC khi mở: nếu không cửa sổ nào mở được thì nhánh dưới hiện lại
            // ngay, người dùng chỉ thấy một cái nháy.
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
                    "Could not open a viewing session - check the address and that the other "
                    "machine is sharing.");
            }
        });
    }).detach();
}

void MainWindow::OnDestroy(GtkWidget*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    self->alive_->store(false);
    delete self;
}
