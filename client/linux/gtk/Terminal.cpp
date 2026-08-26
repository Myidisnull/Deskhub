#include "gtk/Terminal.h"

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "deskhub/ui/Strings.h"
#include "deskhubp/client/TerminalFeed.h"
#include "deskhubp/ffi/TermGridFill.h"
#include "deskhubp/ffi/TerminalFfi.h"
#include "deskhubp/host/TerminalHost.h"

namespace {

struct TermUi {
    DHTermSession* session = nullptr;
    std::unique_ptr<deskhubp::LocalTerminalFeed> local{};
    GtkWidget* drawArea = nullptr;
    std::vector<DHTermCell> cells;
    DHTermGrid grid{};
    std::string status;
    guint tickId = 0;
    bool ownLoop = false;
};

constexpr int kCellW = 8;
constexpr int kCellH = 16;

void ResizeFeed(TermUi* ui, int width, int height) {
    if (!ui) return;
    const deskhub::TermSize size{uint16_t(std::max(1, width / kCellW)),
        uint16_t(std::max(1, height / kCellH))};
    if (ui->session) dh_term_resize(ui->session, size.cols, size.rows);
    if (ui->local) ui->local->Resize(size);
}

gboolean OnDraw(GtkWidget*, cairo_t* cr, gpointer user) {
    auto* ui = static_cast<TermUi*>(user);
    if (!ui) return FALSE;
    cairo_set_source_rgb(cr, 0.06, 0.07, 0.09);
    cairo_paint(cr);
    cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14);
    for (uint16_t row = 0; row < ui->grid.rows; ++row) {
        for (uint16_t col = 0; col < ui->grid.cols; ++col) {
            const size_t idx = size_t(row) * ui->grid.cols + col;
            if (idx >= ui->cells.size()) continue;
            const DHTermCell& cell = ui->cells[idx];
            cairo_set_source_rgb(cr, cell.bgR / 255.0, cell.bgG / 255.0, cell.bgB / 255.0);
            cairo_rectangle(cr, col * kCellW, row * kCellH, kCellW, kCellH);
            cairo_fill(cr);
            if (cell.codepoint == 32) continue;
            char buf[8]{};
            const int n = g_unichar_to_utf8(cell.codepoint, buf);
            if (n <= 0) continue;
            cairo_set_source_rgb(cr, cell.fgR / 255.0, cell.fgG / 255.0, cell.fgB / 255.0);
            cairo_move_to(cr, col * kCellW, row * kCellH + 12);
            cairo_show_text(cr, buf);
        }
    }
    if (ui->grid.cursorVisible) {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
        cairo_rectangle(cr, ui->grid.cursorCol * kCellW, ui->grid.cursorRow * kCellH, kCellW,
            kCellH);
        cairo_fill(cr);
    }
    if (!ui->status.empty() && ui->drawArea) {
        cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
        cairo_move_to(cr, 4, gtk_widget_get_allocated_height(ui->drawArea) - 6);
        cairo_show_text(cr, ui->status.c_str());
    }
    return FALSE;
}

gboolean OnTick(gpointer user) {
    auto* ui = static_cast<TermUi*>(user);
    if (!ui) return G_SOURCE_CONTINUE;

    if (ui->local) {
        if (!ui->local->Alive()) {
            ui->status = deskhub::ui::kTerminalClosed;
            if (ui->drawArea) gtk_widget_queue_draw(ui->drawArea);
            return G_SOURCE_CONTINUE;
        }
        const deskhub::term::TerminalSnapshot shot = ui->local->Snapshot(0);
        const uint32_t count = uint32_t(shot.size.rows) * uint32_t(shot.size.cols);
        ui->cells.assign(size_t(std::max(count, 1u)), DHTermCell{});
        deskhubp::FillTermGrid(shot, ui->cells.data(), uint32_t(ui->cells.size()), &ui->grid);
        if (ui->drawArea) gtk_widget_queue_draw(ui->drawArea);
        return G_SOURCE_CONTINUE;
    }

    if (!ui->session) return G_SOURCE_CONTINUE;
    DHTermGrid grid{};
    if (!dh_term_grid(ui->session, 0, nullptr, 0, &grid)) return G_SOURCE_CONTINUE;
    const uint32_t count = uint32_t(grid.rows) * uint32_t(grid.cols);
    ui->cells.assign(size_t(count), DHTermCell{});
    if (count > 0) dh_term_grid(ui->session, 0, ui->cells.data(), count, &grid);
    ui->grid = grid;
    char msg[512];
    const int n = dh_term_message(ui->session, msg, int(sizeof(msg)));
    if (n > 0) ui->status.assign(msg, size_t(std::min(n, int(sizeof(msg) - 1))));
    if (ui->drawArea) gtk_widget_queue_draw(ui->drawArea);
    return G_SOURCE_CONTINUE;
}

gboolean OnConfigure(GtkWidget* widget, GdkEventConfigure*, gpointer user) {
    ResizeFeed(static_cast<TermUi*>(user), gtk_widget_get_allocated_width(widget),
        gtk_widget_get_allocated_height(widget));
    return FALSE;
}

void SendKey(TermUi* ui, int32_t key, uint32_t codepoint = 0, bool ctrl = false) {
    if (!ui) return;
    if (ui->session) {
        dh_term_send_key(ui->session, key, codepoint, false, false, ctrl);
        return;
    }
    if (!ui->local) return;
    deskhub::term::TermKeyEvent event{};
    if (!deskhubp::DecodeTermKey(key, codepoint, false, false, ctrl, event)) return;
    ui->local->SendKey(event);
}

gboolean OnKey(GtkWidget*, GdkEventKey* event, gpointer user) {
    auto* ui = static_cast<TermUi*>(user);
    if (!ui || (!ui->session && !ui->local)) return FALSE;
    const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    switch (event->keyval) {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            SendKey(ui, DHTermKeyEnter);
            return TRUE;
        case GDK_KEY_BackSpace:
            SendKey(ui, DHTermKeyBackspace);
            return TRUE;
        case GDK_KEY_Tab:
            SendKey(ui, DHTermKeyTab);
            return TRUE;
        case GDK_KEY_Escape:
            SendKey(ui, DHTermKeyEscape);
            return TRUE;
        case GDK_KEY_Up:
            SendKey(ui, DHTermKeyUp);
            return TRUE;
        case GDK_KEY_Down:
            SendKey(ui, DHTermKeyDown);
            return TRUE;
        case GDK_KEY_Left:
            SendKey(ui, DHTermKeyLeft);
            return TRUE;
        case GDK_KEY_Right:
            SendKey(ui, DHTermKeyRight);
            return TRUE;
        case GDK_KEY_Home:
            SendKey(ui, DHTermKeyHome);
            return TRUE;
        case GDK_KEY_End:
            SendKey(ui, DHTermKeyEnd);
            return TRUE;
        case GDK_KEY_Page_Up:
            SendKey(ui, DHTermKeyPageUp);
            return TRUE;
        case GDK_KEY_Page_Down:
            SendKey(ui, DHTermKeyPageDown);
            return TRUE;
        case GDK_KEY_Delete:
            SendKey(ui, DHTermKeyDelete);
            return TRUE;
        default:
            break;
    }
    const gunichar ch = gdk_keyval_to_unicode(event->keyval);
    if (ch >= 32) SendKey(ui, DHTermKeyChar, ch, ctrl);
    return TRUE;
}

void OnDestroy(GtkWidget*, gpointer user) {
    auto* ui = static_cast<TermUi*>(user);
    if (!ui) return;
    if (ui->tickId) g_source_remove(ui->tickId);
    if (ui->session) dh_term_stop(ui->session);
    if (ui->local) ui->local->Shutdown();
    delete ui;
}

GtkWidget* BuildTermWindow(TermUi* ui, const char* title) {
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 560);

    GtkWidget* area = gtk_drawing_area_new();
    ui->drawArea = area;
    gtk_widget_set_can_focus(area, TRUE);
    gtk_widget_grab_focus(area);
    gtk_container_add(GTK_CONTAINER(window), area);

    g_signal_connect(area, "draw", G_CALLBACK(OnDraw), ui);
    g_signal_connect(area, "configure-event", G_CALLBACK(OnConfigure), ui);
    g_signal_connect(area, "key-press-event", G_CALLBACK(OnKey), ui);
    g_signal_connect(window, "destroy", G_CALLBACK(OnDestroy), ui);
    ui->tickId = g_timeout_add(33, OnTick, ui);
    return window;
}

}

bool RunTerminal(const std::string& addrUtf8, const std::string& passcode) {
    DHTermCallbacks callbacks{};
    DHTermSession* session =
        dh_term_open(addrUtf8.c_str(), passcode.c_str(), 100, 30, &callbacks);
    if (!session) return false;

    auto* ui = new TermUi;
    ui->session = session;
    ui->ownLoop = true;

    GtkWidget* window = BuildTermWindow(ui, addrUtf8.c_str());

    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer p) {
        g_main_loop_quit(static_cast<GMainLoop*>(p));
    }),
        loop);

    gtk_widget_show_all(window);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return true;
}

bool RunLocalTerminal(deskhubp::TerminalHost& host, uint32_t termId) {
    if (!host.LocalAlive(termId)) return false;

    auto* ui = new TermUi;
    ui->local = std::make_unique<deskhubp::LocalTerminalFeed>(host, termId);
    ui->status = deskhub::ui::kTerminalAttachedHere;

    GtkWidget* window = BuildTermWindow(ui, deskhub::ui::kTerminalLocalWindowTitle);
    gtk_widget_show_all(window);
    return true;
}
