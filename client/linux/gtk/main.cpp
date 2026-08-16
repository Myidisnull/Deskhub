#include <gtk/gtk.h>

#include <limits.h>
#include <unistd.h>

#include <string>

#include "gtk/MainWindow.h"

namespace {

std::string ExecutableDir() {
    char path[PATH_MAX];
    const ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0) return {};
    path[len] = '\0';
    const std::string full(path);
    const size_t slash = full.find_last_of('/');
    return slash == std::string::npos ? std::string() : full.substr(0, slash);
}

void InstallWindowIcon() {
    gtk_window_set_default_icon_name("deskhub");
    if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "deskhub")) return;

    const std::string dir = ExecutableDir();
    if (dir.empty()) return;
    gtk_window_set_default_icon_from_file((dir + "/deskhub-256.png").c_str(), nullptr);
}

void OnActivate(GtkApplication* app, gpointer) {
    InstallWindowIcon();
    MainWindow::Open(app);
}

}

int main(int argc, char** argv) {
    GtkApplication* app = gtk_application_new("com.manhpham.deskhub",
        G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(OnActivate), nullptr);
    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
