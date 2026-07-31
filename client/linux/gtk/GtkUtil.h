#pragma once
#include <gtk/gtk.h>

#include <functional>
#include <string>
#include <utility>

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

inline void ShowMessage(GtkWindow* parent, GtkMessageType type, const char* title,
    const std::string& detail) {
    GtkWidget* dlg = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, type, GTK_BUTTONS_CLOSE,
        "%s", title);
    if (!detail.empty())
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", detail.c_str());
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

inline void ShowError(GtkWindow* parent, const char* title, const std::string& detail) {
    ShowMessage(parent, GTK_MESSAGE_ERROR, title, detail);
}

inline void ShowWarning(GtkWindow* parent, const char* title, const std::string& detail) {
    ShowMessage(parent, GTK_MESSAGE_WARNING, title, detail);
}

inline void ShowInfo(GtkWindow* parent, const char* title, const std::string& detail) {
    ShowMessage(parent, GTK_MESSAGE_INFO, title, detail);
}
