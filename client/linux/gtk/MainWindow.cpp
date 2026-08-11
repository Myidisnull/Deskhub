#include "gtk/MainWindow.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "deskhubp/media/DisplayEnum.h"
#include "gtk/GtkUtil.h"
#include "gtk/PasscodeDialog.h"
#include "gtk/ViewerWindow.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/system/AppDataFile.h"
#include "deskhubp/system/UiSettingsStore.h"

#include "deskhub/media/QualityPreset.h"
#include "deskhub/media/SourceLabel.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/ConnectFlow.h"
#include "deskhub/session/ShareFlow.h"
#include "deskhub/ui/Strings.h"

namespace {

namespace ui = deskhub::ui;

constexpr const char* kRecentDevicesFile = "recent-devices.txt";

constexpr int kWindowW = 1040;
constexpr int kWindowH = 700;
constexpr int kSidebarW = 180;
constexpr int kNavH = 42;
constexpr int kListH = 130;
constexpr int kPad = 16;
constexpr int kHintWrapChars = 64;
constexpr int kPrimaryButtonH = 46;

constexpr guint kRescanDelayMs = deskhubp::kLanRescanSecs * 1000;

constexpr int kHostActionWidth = 104;
constexpr int kHostActionHeight = 26;
constexpr int kHostCellGap = 8;
constexpr int kHostRowGap = 6;

struct HostColumn {
    const char* title;
    int width;
    float align;
};

const HostColumn kHostColumns[] = {{"Source", 140, 0.f}, {"Size", 80, 0.f},
    {"Viewers", 58, 1.f}, {"Client", 120, 0.f}, {"Capture", 58, 1.f}, {"Send", 50, 1.f},
    {"Mbps", 55, 1.f}, {"RTT", 55, 1.f}};

constexpr const char* kOnlineColour = "#00913c";
constexpr const char* kOfflineColour = "#c82828";
constexpr const char* kUnknownColour = "#787878";

const char* const kPageLabels[] = {ui::kSidebarHost, ui::kSidebarClient, ui::kSidebarSettings};

const char* const kStyleSheet =
    ".deskhub-sidebar { background-color: #1f2937; }"
    ".deskhub-sidebar-title { color: #ffffff; font-weight: bold; font-size: 1.6em; }"
    ".deskhub-nav { background-image: none; background-color: transparent; border: none;"
    " box-shadow: none; color: #d1d5db; font-size: 1.1em; padding: 0 16px; border-radius: 8px; }"
    ".deskhub-nav:hover { background-color: #374151; }"
    ".deskhub-nav:active { background-color: #374151; }"
    ".deskhub-nav-selected { background-color: #2563eb; color: #ffffff; font-weight: bold; }"
    ".deskhub-nav-selected:hover { background-color: #2563eb; }"
    ".deskhub-nav-selected:active { background-color: #2563eb; }"
    ".deskhub-page { background-color: #ffffff; }"
    ".deskhub-page-body { padding: 16px; }"
    ".deskhub-heading { font-weight: bold; font-size: 1.35em; color: #111827; }"
    ".deskhub-section { font-weight: bold; font-size: 1.1em; color: #111827; }"
    ".deskhub-hint { color: #6b7280; }"
    ".deskhub-footnote { color: #94a3b8; }"
    ".deskhub-link { color: #d1d5db; background-color: transparent; border: none; }"
    ".deskhub-link:hover { background-color: transparent; }"
    ".deskhub-link:active { background-color: transparent; }"
    ".deskhub-banner { padding: 10px; border-left: 4px solid #6b7280;"
    " background-color: #f3f4f6; }"
    ".deskhub-banner-busy { border-left-color: #2563eb; background-color: #ebf3ff; }"
    ".deskhub-banner-live { border-left-color: #00913c; background-color: #e8faef; }"
    ".deskhub-banner-state { font-weight: bold; font-size: 1.1em; color: #6b7280; }"
    ".deskhub-banner-state-busy { color: #2563eb; }"
    ".deskhub-banner-state-live { color: #00913c; }"
    ".deskhub-primary { font-weight: bold; color: #ffffff; background-image: none;"
    " background-color: #2563eb; border: none; }"
    ".deskhub-primary:hover { background-color: #2563eb; }"
    ".deskhub-primary:active { background-color: #1d4ed8; }"
    ".deskhub-primary:disabled { background-color: #93c5fd; color: #ffffff; }"
    ".deskhub-primary-stop { background-color: #c82828; }"
    ".deskhub-primary-stop:hover { background-color: #c82828; }"
    ".deskhub-primary-stop:active { background-color: #a51f1f; }"
    ".deskhub-row-action { color: #ffffff; background-image: none; border: none;"
    " padding: 2px 10px; border-radius: 4px; }"
    ".deskhub-row-action-stop { background-color: #c82828; }"
    ".deskhub-row-action-stop:hover { background-color: #c82828; }"
    ".deskhub-row-action-stop:active { background-color: #a51f1f; }"
    ".deskhub-row-action-kick { background-color: #ca6c08; }"
    ".deskhub-row-action-kick:hover { background-color: #ca6c08; }"
    ".deskhub-row-action-kick:active { background-color: #a85a06; }"
    ".deskhub-row-header { color: #6b7280; font-weight: bold; font-size: 0.85em; }"
    ".deskhub-row-cell { color: #111827; }"
    ".deskhub-row-cell-online { color: #00913c; }"
    "window { background-color: #ffffff; color: #111827; }"
    "viewport { background-color: #ffffff; }"
    "entry { background-color: #ffffff; color: #111827; caret-color: #111827;"
    " background-image: none; border: 1px solid #d1d5db; border-radius: 6px;"
    " padding: 4px 8px; }"
    "entry:focus { border-color: #2563eb; }"
    "entry selection { background-color: #2563eb; color: #ffffff; }"
    "spinbutton { background-color: #ffffff; color: #111827; background-image: none;"
    " border: 1px solid #d1d5db; border-radius: 6px; }"
    "spinbutton entry { border: none; }"
    "spinbutton button { background-color: #f3f4f6; color: #111827; background-image: none;"
    " border: none; }"
    "button { background-color: #f9fafb; color: #111827; background-image: none;"
    " border: 1px solid #d1d5db; border-radius: 6px; box-shadow: none; text-shadow: none; }"
    "button:hover { background-color: #f3f4f6; }"
    "button:active { background-color: #e5e7eb; }"
    "button:disabled { color: #9ca3af; }"
    "button.combo { background-color: #ffffff; }"
    "button.titlebutton { background-color: transparent; border: none; }"
    "button.titlebutton:hover { background-color: transparent; }"
    "menu { background-color: #ffffff; }"
    "menuitem { color: #111827; }"
    "menuitem:hover { background-color: #2563eb; color: #ffffff; }"
    "check { background-color: #ffffff; background-image: none; border: 1px solid #9ca3af; }"
    "check:checked { background-color: #2563eb; border-color: #2563eb; color: #ffffff;"
    " background-image: none; }"
    "treeview.view { background-color: #ffffff; color: #111827; }"
    "treeview.view:selected { background-color: #2563eb; color: #ffffff; }"
    "treeview header button { background-color: #f9fafb; color: #6b7280; border: none;"
    " border-radius: 0; }"
    "scrolledwindow { border-color: #d1d5db; }"
    "scrollbar { background-color: #f3f4f6; border: none; margin: 0; }"
    "scrollbar contents, scrollbar trough { background-color: #f3f4f6; border: none;"
    " margin: 0; }"
    "scrollbar slider { background-color: #9ca3af; border: none; border-radius: 8px;"
    " min-width: 8px; min-height: 8px; }"
    "scrollbar slider:hover { background-color: #6b7280; }";

void AddClass(GtkWidget* widget, const char* name) {
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), name);
}

void RemoveClass(GtkWidget* widget, const char* name) {
    gtk_style_context_remove_class(gtk_widget_get_style_context(widget), name);
}

void InstallStyles() {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, kStyleSheet, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

GtkWidget* Label(const std::string& text) {
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.f);
    return label;
}

GtkWidget* StyledLabel(const std::string& text, const char* cssClass) {
    GtkWidget* label = Label(text);
    AddClass(label, cssClass);
    return label;
}

GtkWidget* Heading(const char* text) {
    return StyledLabel(text, "deskhub-heading");
}

GtkWidget* Section(const char* text) {
    return StyledLabel(text, "deskhub-section");
}

GtkWidget* HeadingRow(const char* heading, GCallback onRefresh, gpointer user) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(row), Heading(heading), FALSE, FALSE, 0);

    GtkWidget* refresh = gtk_button_new_with_label(ui::kRefreshNow);
    gtk_widget_set_size_request(refresh, 110, 30);
    g_signal_connect(refresh, "clicked", onRefresh, user);
    gtk_box_pack_end(GTK_BOX(row), refresh, FALSE, FALSE, 0);
    return row;
}

GtkWidget* Hint(const std::string& text) {
    GtkWidget* label = StyledLabel(text, "deskhub-hint");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), kHintWrapChars);
    return label;
}

GtkWidget* PasscodeEntry(const std::string& value) {
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), value.c_str());
    gtk_entry_set_width_chars(GTK_ENTRY(entry), gint(deskhub::kPasscodeDigits));
    gtk_entry_set_max_length(GTK_ENTRY(entry), gint(deskhub::kPasscodeDigits));
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_DIGITS);
    return entry;
}

GtkWidget* Spin(uint32_t value, uint32_t maxValue) {
    GtkWidget* spin = gtk_spin_button_new_with_range(1, double(maxValue), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), double(value));
    return spin;
}

GtkWidget* WrapPage(GtkWidget* content) {
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll), FALSE);
    AddClass(scroll, "deskhub-page");
    AddClass(content, "deskhub-page");
    AddClass(content, "deskhub-page-body");
    gtk_container_add(GTK_CONTAINER(scroll), content);
    return scroll;
}

void AddColumn(GtkWidget* view, const char* title, int textColumn, int colourColumn,
    int width, float align) {
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", align, nullptr);
    GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(title, renderer, "text",
        textColumn, "foreground", colourColumn, nullptr);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column, width);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_alignment(column, align);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
}

GtkWidget* HostCell(const char* cssClass, int width, float align) {
    GtkWidget* label = StyledLabel(std::string(), cssClass);
    gtk_label_set_xalign(GTK_LABEL(label), align);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(label, width, -1);
    return label;
}

GtkWidget* ListFrame(GtkWidget* view, int height) {
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll), FALSE);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_widget_set_size_request(scroll, -1, height);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    return scroll;
}

std::string FormatLastConnected(int64_t unixTime) {
    if (unixTime <= 0) return {};
    const std::time_t stamp = std::time_t(unixTime);
    std::tm parts{};
    if (!localtime_r(&stamp, &parts)) return {};
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &parts) == 0) return {};
    return std::string(buf);
}

bool HostKeyOf(const std::string& addr, uint64_t& key) {
    NetAddr parsed{};
    if (!ParseNetAddr(addr, parsed)) return false;
    key = parsed.Pack();
    return true;
}

std::vector<std::string> AddressesOf(const std::vector<ui::RecentDevice>& devices) {
    std::vector<std::string> out;
    out.reserve(devices.size());
    for (const auto& device : devices) out.push_back(device.addr);
    return out;
}

bool PickSources(GtkWindow* parent, const std::vector<deskhub::SourceInfo>& sources,
    std::vector<deskhub::SourceInfo>& out) {
    out.clear();
    if (sources.empty()) return false;
    if (!deskhub::DecideAfterSourceQuery(sources).showPicker) {
        out = sources;
        return true;
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons(ui::kPickerTitle, parent, GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL, "_View", GTK_RESPONSE_ACCEPT, nullptr);
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

    gtk_box_pack_start(GTK_BOX(box), ListFrame(tree, 220), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), Label(ui::kPickerEachWindow), FALSE, FALSE, 0);
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

void MainWindow::LoadSettings() {
    settings_ = deskhubp::LoadUiSettings();
    recent_ = ui::ParseRecentDevices(deskhubp::ReadAppDataFile(kRecentDevicesFile));
}

uint16_t MainWindow::Port() const {
    return uint16_t(settings_.port);
}

void MainWindow::PostToUi(std::function<void()> fn) {
    RunOnMain([alive = alive_, fn = std::move(fn)] {
        if (alive->load()) fn();
    });
}

void MainWindow::Build(GtkApplication* app) {
    loadingSettings_ = true;
    LoadSettings();
    InstallStyles();

    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_), ui::kAppTitle);
    gtk_window_set_default_size(GTK_WINDOW(window_), kWindowW, kWindowH);
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
    g_signal_connect(window_, "destroy", G_CALLBACK(OnDestroy), this);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window_), root);

    gtk_box_pack_start(GTK_BOX(root), BuildSidebar(), FALSE, FALSE, 0);

    stack_ = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(stack_), BuildHostPage(), "host");
    gtk_stack_add_named(GTK_STACK(stack_), BuildClientPage(), "client");
    gtk_stack_add_named(GTK_STACK(stack_), BuildSettingsPage(), "settings");
    gtk_box_pack_start(GTK_BOX(root), stack_, TRUE, TRUE, 0);

    loadingSettings_ = false;

    RefreshRecentList();
    StartPoller();
    StartScan();

    gtk_widget_show_all(window_);
    SelectPage(kPageClient);
}

GtkWidget* MainWindow::BuildSidebar() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    AddClass(box, "deskhub-sidebar");
    gtk_widget_set_size_request(box, kSidebarW, -1);

    GtkWidget* title = StyledLabel("Deskhub", "deskhub-sidebar-title");
    gtk_widget_set_margin_start(title, kPad);
    gtk_widget_set_margin_end(title, kPad);
    gtk_widget_set_margin_top(title, kPad);
    gtk_widget_set_margin_bottom(title, kPad);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    for (int i = 0; i < kPageCount; ++i) {
        GtkWidget* item = gtk_button_new_with_label(kPageLabels[i]);
        AddClass(item, "deskhub-nav");
        gtk_widget_set_size_request(item, -1, kNavH);
        gtk_widget_set_margin_start(item, 10);
        gtk_widget_set_margin_end(item, 10);
        gtk_widget_set_margin_bottom(item, 10);
        if (GtkWidget* child = gtk_bin_get_child(GTK_BIN(item)))
            gtk_label_set_xalign(GTK_LABEL(child), 0.f);
        g_object_set_data(G_OBJECT(item), "deskhub-page", GINT_TO_POINTER(i));
        g_signal_connect(item, "clicked", G_CALLBACK(OnNavClicked), this);
        gtk_box_pack_start(GTK_BOX(box), item, FALSE, FALSE, 0);
        navButtons_[i] = item;
    }

    GtkWidget* filler = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(box), filler, TRUE, TRUE, 0);

    GtkWidget* link = gtk_link_button_new_with_label(ui::kProjectUrl, ui::kProjectLinkLabel);
    AddClass(link, "deskhub-link");
    gtk_widget_set_halign(link, GTK_ALIGN_START);
    gtk_widget_set_margin_start(link, kPad - 8);
    gtk_widget_set_margin_end(link, kPad);
    gtk_box_pack_start(GTK_BOX(box), link, FALSE, FALSE, 0);

    GtkWidget* version = StyledLabel(ui::VersionLine(), "deskhub-footnote");
    gtk_widget_set_margin_start(version, kPad);
    gtk_widget_set_margin_end(version, kPad);
    gtk_widget_set_margin_bottom(version, kPad);
    gtk_box_pack_start(GTK_BOX(box), version, FALSE, FALSE, 0);

    return box;
}

GtkWidget* MainWindow::BuildHostPage() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    gtk_box_pack_start(GTK_BOX(box), Heading(ui::kHostHeading), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), Hint(ui::kHostIpIntro), FALSE, FALSE, 0);

    const auto addrs = ListLocalIPv4();
    if (addrs.empty()) {
        gtk_box_pack_start(GTK_BOX(box), Label(ui::kNoNetworkAddress), FALSE, FALSE, 0);
    } else {
        GtkWidget* grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
        int row = 0;
        for (const auto& a : addrs) {
            gtk_grid_attach(GTK_GRID(grid), Label(a.name), 0, row, 1, 1);

            GtkWidget* ip = Label(a.ip);
            AddClass(ip, "deskhub-section");
            gtk_label_set_selectable(GTK_LABEL(ip), TRUE);
            gtk_widget_set_hexpand(ip, TRUE);
            gtk_grid_attach(GTK_GRID(grid), ip, 1, row, 1, 1);

            GtkWidget* copy = gtk_button_new_with_label("Copy");
            gtk_widget_set_size_request(copy, 84, 32);
            g_object_set_data_full(G_OBJECT(copy), "deskhub-ip", g_strdup(a.ip.c_str()), g_free);
            g_signal_connect(copy, "clicked", G_CALLBACK(OnCopyClicked), this);
            gtk_grid_attach(GTK_GRID(grid), copy, 2, row, 1, 1);
            ++row;
        }
        gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
    }

    hostBanner_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    AddClass(hostBanner_, "deskhub-banner");
    hostStateLabel_ = StyledLabel(std::string(), "deskhub-banner-state");
    gtk_box_pack_start(GTK_BOX(hostBanner_), hostStateLabel_, FALSE, FALSE, 0);
    hostStatusLabel_ = StyledLabel(std::string(), "deskhub-hint");
    gtk_label_set_line_wrap(GTK_LABEL(hostStatusLabel_), TRUE);
    gtk_box_pack_start(GTK_BOX(hostBanner_), hostStatusLabel_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), hostBanner_, FALSE, FALSE, 0);

    hostGrid_ = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(hostGrid_), kHostCellGap);
    gtk_grid_set_row_spacing(GTK_GRID(hostGrid_), kHostRowGap);
    gtk_widget_set_valign(hostGrid_, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), ListFrame(hostGrid_, kListH + 40), TRUE, TRUE, 0);
    RebuildHostRowWidgets();

    hostHintLabel_ = Hint(ui::kPickDisplaysPortalHint);
    gtk_box_pack_start(GTK_BOX(box), hostHintLabel_, FALSE, FALSE, 0);

    shareButton_ = gtk_button_new_with_label(ui::kStartSharing);
    AddClass(shareButton_, "deskhub-primary");
    gtk_widget_set_size_request(shareButton_, -1, kPrimaryButtonH);
    g_signal_connect(shareButton_, "clicked", G_CALLBACK(OnShareClicked), this);
    gtk_box_pack_start(GTK_BOX(box), shareButton_, FALSE, FALSE, 0);

    ShowIdleHostState();
    return WrapPage(box);
}

GtkWidget* MainWindow::BuildClientPage() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    gtk_box_pack_start(GTK_BOX(box), Heading(ui::kClientHeading), FALSE, FALSE, 0);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    gtk_grid_attach(GTK_GRID(grid), Label(ui::kClientIpPrompt), 0, 0, 1, 1);
    addressEntry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(addressEntry_), ui::kClientIpPlaceholder);
    gtk_entry_set_width_chars(GTK_ENTRY(addressEntry_), 26);
    g_signal_connect(addressEntry_, "activate", G_CALLBACK(OnAddressActivate), this);
    gtk_grid_attach(GTK_GRID(grid), addressEntry_, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), Label(ui::kUdpPortLabel), 0, 1, 1, 1);
    portEntry_ = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(portEntry_), std::to_string(deskhub::kDeskhubPort).c_str());
    gtk_entry_set_width_chars(GTK_ENTRY(portEntry_), 6);
    gtk_widget_set_halign(portEntry_, GTK_ALIGN_START);
    g_signal_connect(portEntry_, "activate", G_CALLBACK(OnAddressActivate), this);
    gtk_grid_attach(GTK_GRID(grid), portEntry_, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), Label(ui::kClientPasscodePrompt), 0, 2, 1, 1);
    passcodeEntry_ = PasscodeEntry(std::string());
    gtk_widget_set_tooltip_text(passcodeEntry_, ui::kClientPasscodeHint);
    gtk_widget_set_halign(passcodeEntry_, GTK_ALIGN_START);
    g_signal_connect(passcodeEntry_, "activate", G_CALLBACK(OnAddressActivate), this);
    gtk_grid_attach(GTK_GRID(grid), passcodeEntry_, 1, 2, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);

    connectButton_ = gtk_button_new_with_label("Connect");
    AddClass(connectButton_, "deskhub-primary");
    gtk_widget_set_size_request(connectButton_, -1, kPrimaryButtonH);
    g_signal_connect(connectButton_, "clicked", G_CALLBACK(OnConnectClicked), this);
    gtk_box_pack_start(GTK_BOX(box), connectButton_, FALSE, FALSE, 0);

    controlCheck_ = gtk_check_button_new_with_label(ui::kRequestControlLabel);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controlCheck_), settings_.clientControl);
    g_signal_connect(controlCheck_, "toggled", G_CALLBACK(OnSettingChanged), this);
    gtk_box_pack_start(GTK_BOX(box), controlCheck_, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box),
        HeadingRow(ui::kLanDevicesHeading, G_CALLBACK(OnRescanClicked), this), FALSE, FALSE, 0);

    scanStore_ = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* scanView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(scanStore_));
    g_object_unref(scanStore_);
    AddColumn(scanView, "Device", 0, 2, 170, 0.f);
    AddColumn(scanView, "Ping", 1, 2, 70, 1.f);
    g_signal_connect(scanView, "row-activated", G_CALLBACK(OnScanRowActivated), this);
    gtk_box_pack_start(GTK_BOX(box), ListFrame(scanView, kListH), TRUE, TRUE, 0);

    scanStatusLabel_ = Hint(ui::kLanDevicesEmpty);
    gtk_box_pack_start(GTK_BOX(box), scanStatusLabel_, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box),
        HeadingRow(ui::kRecentDevicesHeading, G_CALLBACK(OnRefreshStatusClicked), this), FALSE,
        FALSE, 0);

    recentStore_ = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* recentView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(recentStore_));
    g_object_unref(recentStore_);
    AddColumn(recentView, "Device", 0, 4, 170, 0.f);
    AddColumn(recentView, "Status", 1, 4, 100, 0.f);
    AddColumn(recentView, "Ping", 2, 4, 70, 1.f);
    AddColumn(recentView, "Last connected", 3, 4, 150, 0.f);
    g_signal_connect(recentView, "row-activated", G_CALLBACK(OnRecentRowActivated), this);
    gtk_box_pack_start(GTK_BOX(box), ListFrame(recentView, kListH), TRUE, TRUE, 0);

    recentHintLabel_ = Hint(ui::kRecentDevicesEmpty);
    gtk_box_pack_start(GTK_BOX(box), recentHintLabel_, FALSE, FALSE, 0);

    return WrapPage(box);
}

GtkWidget* MainWindow::BuildSettingsPage() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    gtk_box_pack_start(GTK_BOX(box), Heading(ui::kSettingsHeading), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), Hint(ui::kSettingsHint), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), Section("Video"), FALSE, FALSE, 0);
    GtkWidget* videoGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(videoGrid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(videoGrid), 14);

    gtk_grid_attach(GTK_GRID(videoGrid), Label("FPS"), 0, 0, 1, 1);
    fpsSpin_ = Spin(settings_.fps, ui::kMaxSettingsFps);
    gtk_grid_attach(GTK_GRID(videoGrid), fpsSpin_, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(videoGrid), Label("Bitrate (Mbps)"), 0, 1, 1, 1);
    bitrateSpin_ = Spin(settings_.bitrateMbps, ui::kMaxSettingsBitrateMbps);
    gtk_grid_attach(GTK_GRID(videoGrid), bitrateSpin_, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(videoGrid), Label("Quality"), 0, 2, 1, 1);
    qualityCombo_ = gtk_combo_box_text_new();
    for (const auto& preset : deskhub::media::kQualityPresets)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(qualityCombo_), preset.label);
    gtk_combo_box_set_active(GTK_COMBO_BOX(qualityCombo_),
        gint(deskhub::media::QualityPresetIndex(settings_.maxDim)));
    gtk_grid_attach(GTK_GRID(videoGrid), qualityCombo_, 1, 2, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), videoGrid, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), Section("Connection"), FALSE, FALSE, 0);
    GtkWidget* netGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(netGrid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(netGrid), 14);
    gtk_grid_attach(GTK_GRID(netGrid), Label("UDP port"), 0, 0, 1, 1);
    portSpin_ = Spin(settings_.port, ui::kMaxSettingsPort);
    gtk_grid_attach(GTK_GRID(netGrid), portSpin_, 1, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), netGrid, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), Section("Security"), FALSE, FALSE, 0);
    GtkWidget* securityGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(securityGrid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(securityGrid), 14);
    gtk_grid_attach(GTK_GRID(securityGrid), Label(ui::kPasscodeLabel), 0, 0, 1, 1);
    hostPasscodeEntry_ = PasscodeEntry(settings_.passcode);
    gtk_widget_set_halign(hostPasscodeEntry_, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(securityGrid), hostPasscodeEntry_, 1, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), securityGrid, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), Section("Permissions"), FALSE, FALSE, 0);
    allowInputCheck_ = gtk_check_button_new_with_label(ui::kAllowControlLabel);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(allowInputCheck_), settings_.allowInput);
    gtk_box_pack_start(GTK_BOX(box), allowInputCheck_, FALSE, FALSE, 0);

    g_signal_connect(fpsSpin_, "value-changed", G_CALLBACK(OnSettingChanged), this);
    g_signal_connect(bitrateSpin_, "value-changed", G_CALLBACK(OnSettingChanged), this);
    g_signal_connect(portSpin_, "value-changed", G_CALLBACK(OnSettingChanged), this);
    g_signal_connect(qualityCombo_, "changed", G_CALLBACK(OnSettingChanged), this);
    g_signal_connect(hostPasscodeEntry_, "changed", G_CALLBACK(OnSettingChanged), this);
    g_signal_connect(allowInputCheck_, "toggled", G_CALLBACK(OnSettingChanged), this);

    return WrapPage(box);
}

void MainWindow::SelectPage(int page) {
    static const char* const kNames[] = {"host", "client", "settings"};
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), kNames[page]);
    for (int i = 0; i < kPageCount; ++i) {
        if (i == page) {
            AddClass(navButtons_[i], "deskhub-nav-selected");
        } else {
            RemoveClass(navButtons_[i], "deskhub-nav-selected");
        }
    }
}

void MainWindow::OnNavClicked(GtkButton* b, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    self->SelectPage(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "deskhub-page")));
}

std::string MainWindow::HostPortDetail() const {
    return ui::UdpPortLine(Port()) + ".";
}

void MainWindow::ApplyHostState(HostShareState state, const std::string& detail) {
    const bool sharing = state == HostShareState::kSharing;
    const bool starting = state == HostShareState::kStarting;

    gtk_label_set_text(GTK_LABEL(hostStateLabel_),
        sharing ? ui::kShareStateOn : (starting ? ui::kStartingShare : ui::kShareStateOff));
    gtk_label_set_text(GTK_LABEL(hostStatusLabel_), detail.c_str());

    RemoveClass(hostBanner_, "deskhub-banner-busy");
    RemoveClass(hostBanner_, "deskhub-banner-live");
    RemoveClass(hostStateLabel_, "deskhub-banner-state-busy");
    RemoveClass(hostStateLabel_, "deskhub-banner-state-live");
    if (sharing) {
        AddClass(hostBanner_, "deskhub-banner-live");
        AddClass(hostStateLabel_, "deskhub-banner-state-live");
    } else if (starting) {
        AddClass(hostBanner_, "deskhub-banner-busy");
        AddClass(hostStateLabel_, "deskhub-banner-state-busy");
    }

    gtk_button_set_label(GTK_BUTTON(shareButton_),
        sharing ? ui::kStopSharing : ui::kStartSharing);
    if (sharing) {
        AddClass(shareButton_, "deskhub-primary-stop");
    } else {
        RemoveClass(shareButton_, "deskhub-primary-stop");
    }
}

void MainWindow::ShowIdleHostState() {
    ApplyHostState(HostShareState::kIdle, HostPortDetail());
}

void MainWindow::SaveSettings() {
    settings_.fps = uint32_t(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(fpsSpin_)));
    settings_.bitrateMbps =
        uint32_t(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(bitrateSpin_)));
    settings_.port = uint32_t(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(portSpin_)));
    const gint quality = gtk_combo_box_get_active(GTK_COMBO_BOX(qualityCombo_));
    if (quality >= 0)
        settings_.maxDim = deskhub::media::QualityPresetMaxDim(size_t(quality), settings_.maxDim);
    settings_.allowInput = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(allowInputCheck_));
    settings_.clientControl = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controlCheck_));

    const std::string passcode = ui::TrimAscii(gtk_entry_get_text(GTK_ENTRY(hostPasscodeEntry_)));
    if (deskhub::IsValidPasscode(passcode)) settings_.passcode = passcode;

    deskhubp::SaveUiSettings(settings_);
    if (!hosting_ && !hostStarting_) ShowIdleHostState();
}

void MainWindow::SaveRecentDevices() {
    deskhubp::WriteAppDataFile(kRecentDevicesFile, ui::SerializeRecentDevices(recent_));
}

void MainWindow::OnSettingChanged(GtkWidget*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    if (self->loadingSettings_) return;
    self->SaveSettings();
}

void MainWindow::StartScan() {
    scannedThisRound_.clear();
    const bool started = scanner_.Start(
        Port(), [this](const std::function<void()>& fn) { PostToUi(fn); },
        [this](const deskhubp::ScanHit& hit) { OnScanHit(hit); },
        [this](const deskhubp::ScanProgress& progress) { OnScanProgress(progress); },
        [this](const deskhubp::ScanProgress& progress) { OnScanFinished(progress); });
    if (!started) ScheduleRescan();
}

void MainWindow::RescanNow() {
    if (rescanTimerId_) {
        g_source_remove(rescanTimerId_);
        rescanTimerId_ = 0;
    }
    gtk_label_set_text(GTK_LABEL(scanStatusLabel_), ui::kLanDevicesEmpty);
    scanner_.Cancel();
    StartScan();
}

void MainWindow::ScheduleRescan() {
    if (rescanTimerId_) g_source_remove(rescanTimerId_);
    rescanTimerId_ = g_timeout_add(kRescanDelayMs, OnRescanTimer, this);
}

void MainWindow::OnRescanClicked(GtkButton*, gpointer user) {
    static_cast<MainWindow*>(user)->RescanNow();
}

void MainWindow::OnRefreshStatusClicked(GtkButton*, gpointer user) {
    static_cast<MainWindow*>(user)->RefreshDeviceStatus();
}

gboolean MainWindow::OnRescanTimer(gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    self->rescanTimerId_ = 0;
    self->StartScan();
    return G_SOURCE_REMOVE;
}

void MainWindow::OnScanHit(const deskhubp::ScanHit& hit) {
    scannedThisRound_.push_back(hit.addr);
    RecordProbe(hit.addr, true, hit.rttMs);
    for (deskhubp::ScanHit& known : scanned_) {
        if (known.addr != hit.addr) continue;
        known.rttMs = hit.rttMs;
        RefreshScanList();
        RefreshRecentList();
        return;
    }
    scanned_.push_back(hit);
    RefreshScanList();
    RefreshRecentList();
}

void MainWindow::OnScanProgress(const deskhubp::ScanProgress& progress) {
    const std::string text = ui::ScanningStatus(progress.probed, progress.total, Port());
    gtk_label_set_text(GTK_LABEL(scanStatusLabel_), text.c_str());
}

void MainWindow::OnScanFinished(const deskhubp::ScanProgress& progress) {
    const auto gone = [this](const deskhubp::ScanHit& hit) {
        return std::find(scannedThisRound_.begin(), scannedThisRound_.end(), hit.addr) ==
               scannedThisRound_.end();
    };
    scanned_.erase(std::remove_if(scanned_.begin(), scanned_.end(), gone), scanned_.end());
    RefreshScanList();

    const std::string text =
        ui::LanDevicesNote(scanned_.size(), progress.total, deskhubp::kLanRescanSecs);
    gtk_label_set_text(GTK_LABEL(scanStatusLabel_), text.c_str());
    ScheduleRescan();
}

void MainWindow::RefreshScanList() {
    gtk_list_store_clear(scanStore_);
    for (const deskhubp::ScanHit& hit : scanned_) {
        const deskhubp::DeviceStatus* probe = ProbeFor(hit.addr);
        const bool online = !probe || probe->online;

        GtkTreeIter it;
        gtk_list_store_append(scanStore_, &it);
        gtk_list_store_set(scanStore_, &it, 0, hit.addr.c_str(), 1,
            online && probe ? ui::PingMs(probe->rttMs).c_str() : "-", 2,
            online ? kOnlineColour : kOfflineColour, -1);
    }
}

void MainWindow::StartPoller() {
    poller_.SetAddresses(AddressesOf(recent_));
    poller_.Start([this](const deskhubp::DeviceStatus& status) {
        PostToUi([this, status] { OnDeviceStatus(status); });
    });
}

void MainWindow::OnDeviceStatus(const deskhubp::DeviceStatus& status) {
    RecordProbe(status.addr, status.online, status.rttMs);
    RefreshRecentList();
    RefreshScanList();
}

void MainWindow::RecordProbe(const std::string& addr, bool online, uint32_t rttMs) {
    uint64_t key = 0;
    if (!HostKeyOf(addr, key)) return;
    probes_[key] = deskhubp::DeviceStatus{addr, online, rttMs};
}

const deskhubp::DeviceStatus* MainWindow::ProbeFor(const std::string& addr) const {
    uint64_t key = 0;
    if (!HostKeyOf(addr, key)) return nullptr;
    const auto found = probes_.find(key);
    return found == probes_.end() ? nullptr : &found->second;
}

void MainWindow::RefreshRecentList() {
    gtk_list_store_clear(recentStore_);
    for (const ui::RecentDevice& device : recent_) {
        const deskhubp::DeviceStatus* probe = ProbeFor(device.addr);
        const bool online = probe && probe->online;

        const char* status = !probe ? ui::kStatusChecking
                                    : (online ? ui::kStatusOnline : ui::kStatusOffline);
        const std::string ping = online ? ui::PingMs(probe->rttMs) : std::string("-");
        const char* colour = !probe ? kUnknownColour : (online ? kOnlineColour : kOfflineColour);

        GtkTreeIter it;
        gtk_list_store_append(recentStore_, &it);
        gtk_list_store_set(recentStore_, &it, 0, device.addr.c_str(), 1, status, 2, ping.c_str(),
            3, FormatLastConnected(device.lastConnectedUnix).c_str(), 4, colour, -1);
    }
    gtk_label_set_text(GTK_LABEL(recentHintLabel_),
        ui::RecentDevicesNote(recent_.size(), deskhubp::kDeviceStatusRoundSecs).c_str());
}

void MainWindow::RefreshDeviceStatus() {
    for (const ui::RecentDevice& device : recent_) {
        uint64_t key = 0;
        if (HostKeyOf(device.addr, key)) probes_.erase(key);
    }
    RefreshRecentList();
    poller_.RefreshNow();
}

void MainWindow::OnScanRowActivated(GtkTreeView*, GtkTreePath* path, GtkTreeViewColumn*,
    gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    const gint* idx = gtk_tree_path_get_indices(path);
    if (!idx || idx[0] < 0 || size_t(idx[0]) >= self->scanned_.size()) return;
    const std::string addr = self->scanned_[size_t(idx[0])].addr;
    self->ConnectWithPrompt(addr, ui::PasscodeForDevice(self->recent_, addr));
}

void MainWindow::OnRecentRowActivated(GtkTreeView*, GtkTreePath* path, GtkTreeViewColumn*,
    gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    const gint* idx = gtk_tree_path_get_indices(path);
    if (!idx || idx[0] < 0 || size_t(idx[0]) >= self->recent_.size()) return;
    const ui::RecentDevice device = self->recent_[size_t(idx[0])];
    self->ConnectWithPrompt(device.addr, device.passcode);
}

void MainWindow::ConnectWithPrompt(const std::string& addr, std::string passcode) {
    std::string target = addr;
    if (!ShowPasscodeDialog(GTK_WINDOW(window_), target, passcode)) return;

    const std::string code = ui::TrimAscii(passcode);
    if (!deskhub::IsValidPasscode(code)) {
        ShowError(GTK_WINDOW(window_), "Deskhub", ui::kPasscodeInvalid);
        return;
    }

    const uint16_t port = ui::AddressPort(target);
    gtk_entry_set_text(GTK_ENTRY(addressEntry_), ui::AddressHost(target).c_str());
    gtk_entry_set_text(GTK_ENTRY(portEntry_),
        std::to_string(port != 0 ? port : deskhub::kDeskhubPort).c_str());
    gtk_entry_set_text(GTK_ENTRY(passcodeEntry_), code.c_str());
    StartConnect(target, code);
}

bool MainWindow::ReadPasscode(GtkWidget* entry, std::string& out) {
    out = ui::TrimAscii(gtk_entry_get_text(GTK_ENTRY(entry)));
    if (deskhub::IsValidPasscode(out)) return true;

    ShowError(GTK_WINDOW(window_), "Deskhub", ui::kPasscodeInvalid);
    gtk_widget_grab_focus(entry);
    out.clear();
    return false;
}

void MainWindow::SetBusy(bool busy, const char* what) {
    gtk_widget_set_sensitive(connectButton_, !busy);
    gtk_button_set_label(GTK_BUTTON(connectButton_), busy && what ? what : "Connect");
}

void MainWindow::ShowAfterSession() {
    gtk_window_present(GTK_WINDOW(window_));
}

void MainWindow::OnAddressActivate(GtkEntry*, gpointer user) {
    OnConnectClicked(nullptr, user);
}

void MainWindow::OnConnectClicked(GtkButton*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);

    const std::string text = ui::TrimAscii(gtk_entry_get_text(GTK_ENTRY(self->addressEntry_)));
    if (text.empty()) {
        ShowWarning(GTK_WINDOW(self->window_), "Deskhub",
            "Enter the host machine's IP address first (e.g., 192.168.1.10).");
        return;
    }

    std::string passcode;
    if (!self->ReadPasscode(self->passcodeEntry_, passcode)) return;

    const uint16_t port =
        ui::PortOrDefault(gtk_entry_get_text(GTK_ENTRY(self->portEntry_)));
    self->StartConnect(ui::AddressWithPort(text, port), passcode);
}

void MainWindow::StartConnect(const std::string& addr, const std::string& passcode) {
    NetAddr server{};
    if (!ParseNetAddr(addr, server)) {
        ShowError(GTK_WINDOW(window_), ("Invalid address: \"" + addr + "\"").c_str(),
            ui::InvalidAddressHint());
        return;
    }

    const bool started = connectDriver_.QueryAsync(
        server, passcode, [this](const std::function<void()>& fn) { PostToUi(fn); },
        [this, addr, passcode](const deskhubp::ConnectOutcome& outcome) {
            OnSourcesReady(addr, passcode, outcome);
        });
    if (started) SetBusy(true, ui::kQueryingSources);
}

void MainWindow::OnSourcesReady(const std::string& addr, const std::string& passcode,
    const deskhubp::ConnectOutcome& outcome) {
    SetBusy(false, nullptr);

    if (outcome.ok) {
        ui::TouchRecentDevice(recent_, addr, int64_t(std::time(nullptr)), passcode);
        SaveRecentDevices();
        poller_.SetAddresses(AddressesOf(recent_));
        RefreshRecentList();
    }

    NetAddr server{};
    if (!ParseNetAddr(addr, server)) return;

    std::vector<deskhub::SourceInfo> picked;
    if (outcome.hasSources()) {
        if (!PickSources(GTK_WINDOW(window_), outcome.sources, picked)) return;
    } else {
        picked = deskhubp::DefaultViewTargets();
    }

    int opened = 0;
    for (const auto& s : picked) {
        if (ViewerWindow::Open(server, s.sourceId, s.name, passcode,
                [this, alive = alive_] {
                    if (!alive->load()) return;
                    if (openViewers_.Closed()) ShowAfterSession();
                })) {
            openViewers_.Opened();
            ++opened;
        }
    }
    if (opened == 0) {
        ShowAfterSession();
        ShowWarning(GTK_WINDOW(window_), "Deskhub", ui::kViewerOpenFailed);
    }
}

void MainWindow::OnCopyClicked(GtkButton* b, gpointer) {
    const char* ip = static_cast<const char*>(g_object_get_data(G_OBJECT(b), "deskhub-ip"));
    if (!ip) return;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), ip, -1);
}

void MainWindow::OnShareClicked(GtkButton*, gpointer user) {
    static_cast<MainWindow*>(user)->OnShare();
}

void MainWindow::OnShare() {
    if (hostStarting_) return;
    if (hosting_) {
        StopHosting();
        return;
    }

    AgentOptions options;
    options.fps = settings_.fps;
    options.bitrateMbps = settings_.bitrateMbps;
    options.maxDim = settings_.maxDim;
    options.port = Port();
    options.allowInput = settings_.allowInput;
    options.passcode = settings_.passcode;

    hostStarting_ = true;
    gtk_widget_set_sensitive(shareButton_, FALSE);
    ApplyHostState(HostShareState::kStarting, "Waiting for the screen-sharing dialog\xE2\x80\xA6");

    std::thread([this, options, alive = alive_] {
        std::vector<AgentSource> sources = deskhubp::ListDisplays();
        const std::string err = sources.empty() ? deskhubp::ListDisplaysError() : std::string();

        RunOnMain([this, sources, options, err, alive]() mutable {
            if (!alive->load()) return;
            hostStarting_ = false;
            gtk_widget_set_sensitive(shareButton_, TRUE);

            if (sources.empty()) {
                ShowIdleHostState();
                if (!err.empty() && err != "cancelled by the user")
                    ShowError(GTK_WINDOW(window_), "Screen capture is not available", err);
                return;
            }

            const deskhub::ShareClampResult clamp = deskhub::ClampShareSources(std::move(sources));
            if (clamp.clamped)
                ShowWarning(GTK_WINDOW(window_), "Deskhub", ui::ShareClampWarning());

            StartHosting(clamp.sources, options);
        });
    }).detach();
}

void MainWindow::StartHosting(const std::vector<AgentSource>& sources,
    const AgentOptions& options) {
    hostStarting_ = true;
    gtk_widget_set_sensitive(shareButton_, FALSE);
    ApplyHostState(HostShareState::kStarting, HostPortDetail());
    ClearHostRows();
    gtk_widget_hide(hostHintLabel_);

    agentDriver_.Join();
    agentDriver_.StartAsync(
        agentLoop_, sources, options,
        [this](const std::function<void()>& fn) { PostToUi(fn); },
        [this, options](bool started, const std::string& error) {
            OnHostStarted(started, error, options);
        });
}

void MainWindow::OnHostStarted(bool started, const std::string& error,
    const AgentOptions& options) {
    hostStarting_ = false;
    gtk_widget_set_sensitive(shareButton_, TRUE);

    if (!started) {
        ShowIdleHostState();
        gtk_widget_show(hostHintLabel_);
        ShowError(GTK_WINDOW(window_), "Deskhub",
            std::string(ui::kShareStartFailed) + ".\n\n" + error);
        return;
    }

    hosting_ = true;

    std::string status = ui::SharingStatusLine(options.port);
    if (!options.passcode.empty()) status += " " + ui::PasscodeNote(options.passcode);
    if (!options.allowInput) status += std::string(" ") + ui::kViewOnlyNote;
    ApplyHostState(HostShareState::kSharing, status);

    if (hostTimerId_) g_source_remove(hostTimerId_);
    hostTimerId_ = g_timeout_add(deskhubp::kAgentStatusPollMs, OnHostTimer, this);
}

void MainWindow::StopHosting() {
    if (hostTimerId_) {
        g_source_remove(hostTimerId_);
        hostTimerId_ = 0;
    }
    agentLoop_.Stop();
    agentDriver_.Join();
    hosting_ = false;
    ShowIdleHostState();
    gtk_widget_show(hostHintLabel_);
    ClearHostRows();
}

gboolean MainWindow::OnHostTimer(gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    if (!self->hosting_) {
        self->hostTimerId_ = 0;
        return G_SOURCE_REMOVE;
    }

    std::vector<AgentSourceStatus> rows;
    const deskhubp::AgentDriveState state = self->agentDriver_.Poll(self->agentLoop_, rows);
    if (state == deskhubp::AgentDriveState::Stopped) {
        self->hostTimerId_ = 0;
        self->StopHosting();
        return G_SOURCE_REMOVE;
    }
    if (state == deskhubp::AgentDriveState::Running) self->UpdateHostRows(rows);
    return G_SOURCE_CONTINUE;
}

MainWindow::HostRowWidgets MainWindow::MakeHostRowWidgets(const ui::HostRow& ref, size_t index) {
    HostRowWidgets widgets{};
    for (int i = 0; i < kHostColumnCount; ++i) {
        widgets.cells[i] = HostCell("deskhub-row-cell", kHostColumns[i].width,
            kHostColumns[i].align);
    }

    widgets.action = gtk_button_new_with_label(
        ref.viewer ? ui::kDisconnectViewerAction : ui::kStopDisplayAction);
    AddClass(widgets.action, "deskhub-row-action");
    AddClass(widgets.action, ref.viewer ? "deskhub-row-action-kick" : "deskhub-row-action-stop");
    gtk_widget_set_size_request(widgets.action, kHostActionWidth, kHostActionHeight);
    gtk_widget_set_valign(widgets.action, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(widgets.action, GTK_ALIGN_END);
    g_object_set_data(G_OBJECT(widgets.action), "deskhub-host-row",
        GINT_TO_POINTER(gint(index)));
    g_signal_connect(widgets.action, "clicked", G_CALLBACK(OnHostRowActionClicked), this);
    return widgets;
}

void MainWindow::RebuildHostRowWidgets() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(hostGrid_));
    for (GList* child = children; child; child = child->next) {
        gtk_widget_destroy(GTK_WIDGET(child->data));
    }
    g_list_free(children);

    for (int i = 0; i < kHostColumnCount; ++i) {
        GtkWidget* title = HostCell("deskhub-row-header", kHostColumns[i].width,
            kHostColumns[i].align);
        gtk_label_set_text(GTK_LABEL(title), kHostColumns[i].title);
        gtk_grid_attach(GTK_GRID(hostGrid_), title, i, 0, 1, 1);
    }

    hostRowWidgets_.clear();
    hostRowWidgets_.reserve(hostRows_.size());
    for (size_t i = 0; i < hostRows_.size(); ++i) {
        hostRowWidgets_.push_back(MakeHostRowWidgets(hostRows_[i], i));
        const HostRowWidgets& widgets = hostRowWidgets_[i];
        const gint row = gint(i) + 1;
        for (int c = 0; c < kHostColumnCount; ++c) {
            gtk_grid_attach(GTK_GRID(hostGrid_), widgets.cells[c], c, row, 1, 1);
        }
        gtk_grid_attach(GTK_GRID(hostGrid_), widgets.action, kHostColumnCount, row, 1, 1);
    }
    gtk_widget_show_all(hostGrid_);
}

void MainWindow::ClearHostRows() {
    hostRows_.clear();
    RebuildHostRowWidgets();
}

void MainWindow::FillHostRow(
    const HostRowWidgets& widgets, const ui::HostRow& ref, const AgentSourceStatus& status) {
    const ui::HostRowCells cells = ui::HostRowText(ref, status);
    const std::string* text[kHostColumnCount] = {&cells.source, &cells.size, &cells.viewers,
        &cells.client, &cells.capture, &cells.send, &cells.mbps, &cells.rtt};
    for (int i = 0; i < kHostColumnCount; ++i) {
        gtk_label_set_text(GTK_LABEL(widgets.cells[i]), text[i]->c_str());
        if (cells.online) {
            AddClass(widgets.cells[i], "deskhub-row-cell-online");
        } else {
            RemoveClass(widgets.cells[i], "deskhub-row-cell-online");
        }
    }
}

void MainWindow::UpdateHostRows(const std::vector<AgentSourceStatus>& rows) {
    std::vector<ui::HostRow> refs = ui::BuildHostRows(rows);
    if (refs != hostRows_) {
        hostRows_ = std::move(refs);
        RebuildHostRowWidgets();
    }

    for (size_t i = 0; i < hostRows_.size() && i < hostRowWidgets_.size(); ++i) {
        const AgentSourceStatus* source = ui::FindHostSource(rows, hostRows_[i].sourceId);
        if (source) FillHostRow(hostRowWidgets_[i], hostRows_[i], *source);
    }
}

void MainWindow::RunRowAction(const ui::HostRow& row) {
    if (!hosting_) return;
    if (!row.viewer) {
        agentLoop_.StopSource(row.sourceId);
        return;
    }
    NetAddr addr{};
    if (!ParseNetAddr(row.viewerAddr, addr)) return;
    agentLoop_.KickViewer(row.sourceId, addr.Pack());
}

void MainWindow::OnHostRowActionClicked(GtkButton* b, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    const gint index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "deskhub-host-row"));
    if (index < 0 || size_t(index) >= self->hostRows_.size()) return;
    self->RunRowAction(self->hostRows_[size_t(index)]);
}

void MainWindow::OnDestroy(GtkWidget*, gpointer user) {
    auto* self = static_cast<MainWindow*>(user);
    self->alive_->store(false);
    if (self->rescanTimerId_) g_source_remove(self->rescanTimerId_);
    if (self->hostTimerId_) g_source_remove(self->hostTimerId_);
    self->scanner_.Cancel();
    self->agentLoop_.Stop();
    self->agentDriver_.Join();
    self->poller_.Stop();
    delete self;
}
