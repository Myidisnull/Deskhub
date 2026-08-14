#include "TrayIcon.h"

#ifdef DESKHUB_HAVE_APPINDICATOR

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>

#include "deskhub/ui/Strings.h"

namespace {

namespace ui = deskhub::ui;

bool StatusNotifierWatcherPresent() {
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return false;
    GVariant* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
        g_variant_new("(s)", "org.kde.StatusNotifierWatcher"), G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
    gboolean present = FALSE;
    if (reply) {
        g_variant_get(reply, "(b)", &present);
        g_variant_unref(reply);
    }
    g_object_unref(bus);
    return present;
}

}

bool TrayIcon::Attach(const Actions& actions) {
    if (attached_) return true;
    if (!StatusNotifierWatcherPresent()) return false;

    actions_ = actions;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    AppIndicator* indicator =
        app_indicator_new("deskhub", "deskhub", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    G_GNUC_END_IGNORE_DEPRECATIONS

    GtkWidget* menu = gtk_menu_new();

    GtkWidget* toggleWindow = gtk_menu_item_new_with_label(ui::kTrayHideWindow);
    g_signal_connect(toggleWindow, "activate", G_CALLBACK(OnToggleWindowItem), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), toggleWindow);

    GtkWidget* toggleShare = gtk_menu_item_new_with_label(ui::kStartSharing);
    g_signal_connect(toggleShare, "activate", G_CALLBACK(OnToggleShareItem), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), toggleShare);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    GtkWidget* quit = gtk_menu_item_new_with_label(ui::kTrayQuit);
    g_signal_connect(quit, "activate", G_CALLBACK(OnQuitItem), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);

    gtk_widget_show_all(menu);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    app_indicator_set_menu(indicator, GTK_MENU(menu));
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    G_GNUC_END_IGNORE_DEPRECATIONS

    indicator_ = indicator;
    toggleWindowItem_ = toggleWindow;
    toggleShareItem_ = toggleShare;
    attached_ = true;
    RefreshLabels();
    return true;
}

void TrayIcon::Detach() {
    if (!attached_) return;
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    app_indicator_set_status(static_cast<AppIndicator*>(indicator_),
        APP_INDICATOR_STATUS_PASSIVE);
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_object_unref(indicator_);
    indicator_ = nullptr;
    toggleWindowItem_ = nullptr;
    toggleShareItem_ = nullptr;
    attached_ = false;
}

void TrayIcon::SetSharing(bool sharing) {
    sharing_ = sharing;
    RefreshLabels();
}

void TrayIcon::SetWindowVisible(bool visible) {
    windowVisible_ = visible;
    RefreshLabels();
}

void TrayIcon::RefreshLabels() {
    if (!attached_) return;
    gtk_menu_item_set_label(GTK_MENU_ITEM(toggleWindowItem_),
        windowVisible_ ? ui::kTrayHideWindow.get() : ui::kTrayShowWindow.get());
    gtk_menu_item_set_label(GTK_MENU_ITEM(toggleShareItem_),
        sharing_ ? ui::kStopSharing.get() : ui::kStartSharing.get());
}

void TrayIcon::OnToggleWindowItem(void*, void* user) {
    auto* self = static_cast<TrayIcon*>(user);
    if (self->actions_.onToggleWindow) self->actions_.onToggleWindow();
}

void TrayIcon::OnToggleShareItem(void*, void* user) {
    auto* self = static_cast<TrayIcon*>(user);
    if (self->actions_.onToggleShare) self->actions_.onToggleShare();
}

void TrayIcon::OnQuitItem(void*, void* user) {
    auto* self = static_cast<TrayIcon*>(user);
    if (self->actions_.onQuit) self->actions_.onQuit();
}

#else

bool TrayIcon::Attach(const Actions&) {
    return false;
}

void TrayIcon::Detach() {}

void TrayIcon::SetSharing(bool sharing) {
    sharing_ = sharing;
}

void TrayIcon::SetWindowVisible(bool visible) {
    windowVisible_ = visible;
}

void TrayIcon::RefreshLabels() {}

void TrayIcon::OnToggleWindowItem(void*, void*) {}

void TrayIcon::OnToggleShareItem(void*, void*) {}

void TrayIcon::OnQuitItem(void*, void*) {}

#endif
