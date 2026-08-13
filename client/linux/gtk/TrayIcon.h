#pragma once
#include <functional>

class TrayIcon {
public:
    struct Actions {
        std::function<void()> onToggleWindow;
        std::function<void()> onToggleShare;
        std::function<void()> onQuit;
    };

    bool Attach(const Actions& actions);
    void Detach();
    void SetSharing(bool sharing);
    void SetWindowVisible(bool visible);

    bool Attached() const {
        return attached_;
    }

private:
    void RefreshLabels();
    static void OnToggleWindowItem(void* item, void* user);
    static void OnToggleShareItem(void* item, void* user);
    static void OnQuitItem(void* item, void* user);

    Actions actions_;
    bool attached_ = false;
    bool sharing_ = false;
    bool windowVisible_ = true;
    void* indicator_ = nullptr;
    void* toggleWindowItem_ = nullptr;
    void* toggleShareItem_ = nullptr;
};
