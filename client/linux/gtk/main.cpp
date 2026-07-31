#include <gtk/gtk.h>

#include "gtk/MainWindow.h"

namespace {

void OnActivate(GtkApplication* app, gpointer) {
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
