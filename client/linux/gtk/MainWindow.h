#pragma once
#include <gtk/gtk.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class MainWindow {
public:
    static void Open(GtkApplication* app);

private:
    MainWindow() = default;
    ~MainWindow() = default;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void Build(GtkApplication* app);
    GtkWidget* BuildHostBox();
    GtkWidget* BuildClientBox();

    void SetBusy(bool busy, const char* what);
    void HideForSession();
    void ShowAfterSession();

    static void OnShareClicked(GtkButton* b, gpointer user);
    static void OnConnectClicked(GtkButton* b, gpointer user);
    static void OnAddressActivate(GtkEntry* e, gpointer user);
    static void OnCopyClicked(GtkButton* b, gpointer user);
    static void OnExitClicked(GtkButton* b, gpointer user);
    static void OnDestroy(GtkWidget* w, gpointer user);

    GtkWidget* window_ = nullptr;
    GtkWidget* addressEntry_ = nullptr;
    GtkWidget* fpsEntry_ = nullptr;
    GtkWidget* bitrateEntry_ = nullptr;
    GtkWidget* qualityCombo_ = nullptr;
    GtkWidget* shareButton_ = nullptr;
    GtkWidget* connectButton_ = nullptr;
    GtkWidget* statusLabel_ = nullptr;

    int openViewers_ = 0;

    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
