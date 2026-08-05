#pragma once
#include <gtk/gtk.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "deskhub/session/OpenViewers.h"
#include "deskhub/ui/RecentDevices.h"
#include "deskhub/ui/UiSettings.h"
#include "deskhubp/net/DeviceStatusPoller.h"
#include "deskhubp/net/LanScanner.h"
#include "deskhubp/session/AgentDriver.h"
#include "deskhubp/session/AgentLoop.h"
#include "deskhubp/session/ConnectDriver.h"

class MainWindow {
public:
    static void Open(GtkApplication* app);

private:
    enum Page { kPageHost = 0,
        kPageClient = 1,
        kPageSettings = 2,
        kPageCount = 3 };

    struct HostRow {
        bool viewer = false;
        uint8_t sourceId = 0;
        std::string viewerAddr;

        bool operator==(const HostRow&) const = default;
    };

    MainWindow() = default;
    ~MainWindow() = default;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void Build(GtkApplication* app);
    GtkWidget* BuildSidebar();
    GtkWidget* BuildHostPage();
    GtkWidget* BuildClientPage();
    GtkWidget* BuildSettingsPage();
    void SelectPage(int page);

    void LoadSettings();
    void SaveSettings();
    void SaveRecentDevices();

    void StartScan();
    void ScheduleRescan();
    void OnScanHit(const deskhubp::ScanHit& hit);
    void OnScanProgress(const deskhubp::ScanProgress& progress);
    void OnScanFinished(const deskhubp::ScanProgress& progress);
    void RefreshScanList();

    void StartPoller();
    void OnDeviceStatus(const deskhubp::DeviceStatus& status);
    void RefreshRecentList();

    void ConnectWithPrompt(const std::string& addr, std::string passcode);
    void StartConnect(const std::string& addr, const std::string& passcode);
    void OnSourcesReady(const std::string& addr, const std::string& passcode,
        const deskhubp::ConnectOutcome& outcome);
    bool ReadPasscode(GtkWidget* entry, std::string& out);

    void OnShare();
    void StartHosting(const std::vector<AgentSource>& sources, const AgentOptions& options);
    void OnHostStarted(bool started, const std::string& error, const AgentOptions& options);
    void StopHosting();
    void UpdateHostRows(const std::vector<AgentSourceStatus>& rows);
    void UpdateHostButtons();
    bool SelectedHostRow(HostRow& out) const;
    std::string IdleHostStatus() const;
    void SetHostStatus(const std::string& text, bool online);

    void HideForSession();
    void ShowAfterSession();
    void SetBusy(bool busy, const char* what);

    uint16_t Port() const;
    void PostToUi(std::function<void()> fn);

    static void OnNavClicked(GtkButton* b, gpointer user);
    static void OnShareClicked(GtkButton* b, gpointer user);
    static void OnStopDisplayClicked(GtkButton* b, gpointer user);
    static void OnKickViewerClicked(GtkButton* b, gpointer user);
    static void OnConnectClicked(GtkButton* b, gpointer user);
    static void OnAddressActivate(GtkEntry* e, gpointer user);
    static void OnCopyClicked(GtkButton* b, gpointer user);
    static void OnSettingChanged(GtkWidget* w, gpointer user);
    static void OnHostSelectionChanged(GtkTreeSelection* selection, gpointer user);
    static void OnScanRowActivated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* col,
        gpointer user);
    static void OnRecentRowActivated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* col,
        gpointer user);
    static gboolean OnRescanTimer(gpointer user);
    static gboolean OnHostTimer(gpointer user);
    static void OnDestroy(GtkWidget* w, gpointer user);

    GtkWidget* window_ = nullptr;
    GtkWidget* stack_ = nullptr;
    GtkWidget* navButtons_[kPageCount] = {};

    GtkWidget* hostStatusLabel_ = nullptr;
    GtkWidget* hostHintLabel_ = nullptr;
    GtkWidget* hostView_ = nullptr;
    GtkListStore* hostStore_ = nullptr;
    GtkWidget* stopDisplayButton_ = nullptr;
    GtkWidget* kickViewerButton_ = nullptr;
    GtkWidget* shareButton_ = nullptr;

    GtkWidget* addressEntry_ = nullptr;
    GtkWidget* passcodeEntry_ = nullptr;
    GtkWidget* connectButton_ = nullptr;
    GtkWidget* controlCheck_ = nullptr;
    GtkWidget* scanStatusLabel_ = nullptr;
    GtkWidget* recentHintLabel_ = nullptr;
    GtkListStore* scanStore_ = nullptr;
    GtkListStore* recentStore_ = nullptr;

    GtkWidget* fpsSpin_ = nullptr;
    GtkWidget* bitrateSpin_ = nullptr;
    GtkWidget* portSpin_ = nullptr;
    GtkWidget* qualityCombo_ = nullptr;
    GtkWidget* hostPasscodeEntry_ = nullptr;
    GtkWidget* allowInputCheck_ = nullptr;

    deskhub::ui::UiSettings settings_;
    std::vector<deskhub::ui::RecentDevice> recent_;
    std::vector<deskhubp::ScanHit> scanned_;
    std::vector<std::string> scannedThisRound_;
    std::map<std::string, deskhubp::DeviceStatus> statusByAddr_;
    std::vector<HostRow> hostRows_;

    deskhubp::LanScanner scanner_;
    deskhubp::DeviceStatusPoller poller_;
    AgentLoop agentLoop_;
    deskhubp::AgentDriver agentDriver_;

    guint rescanTimerId_ = 0;
    guint hostTimerId_ = 0;
    bool hosting_ = false;
    bool hostStarting_ = false;
    bool loadingSettings_ = false;

    deskhub::OpenViewerCount openViewers_;
    deskhubp::ConnectDriver connectDriver_;

    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
