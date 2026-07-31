#pragma once
#include <gtk/gtk.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "deskhubp/session/AgentLoop.h"

class ShareWindow {
public:
    static void Open(const std::vector<AgentSource>& sources, const AgentOptions& opt,
        std::function<void()> onClosed);

private:
    ShareWindow() = default;
    ~ShareWindow();
    ShareWindow(const ShareWindow&) = delete;
    ShareWindow& operator=(const ShareWindow&) = delete;

    void Build(const std::vector<AgentSource>& sources, const AgentOptions& opt);
    void Refresh();
    void RefreshList(const std::vector<AgentSourceStatus>& rows);

    static gboolean OnTimer(gpointer user);
    static void OnStopClicked(GtkButton* b, gpointer user);
    static void OnDestroy(GtkWidget* w, gpointer user);

    GtkWidget* window_ = nullptr;
    GtkWidget* rowsBox_ = nullptr;
    guint timer_ = 0;

    AgentLoop agent_;
    std::thread starter_;
    bool starting_ = true;

    std::function<void()> onClosed_;

    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
