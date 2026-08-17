#pragma once
#include <gtk/gtk.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "TrayIcon.h"
#include "deskhub/net/PairedDevices.h"
#include "deskhub/session/OpenViewers.h"
#include "deskhub/session/TerminalSession.h"
#include "deskhub/ui/AutoShareGate.h"
#include "deskhub/ui/DeviceRows.h"
#include "deskhub/ui/HostRows.h"
#include "deskhub/ui/RecentDevices.h"
#include "deskhub/ui/UiSettings.h"
#include "deskhubp/net/DeviceStatusPoller.h"
#include "deskhubp/net/LanScanner.h"
#include "deskhubp/session/AgentDriver.h"
#include "deskhubp/session/AgentLoop.h"
#include "deskhubp/session/ConnectDriver.h"
#include "deskhubp/session/TerminalHost.h"

class MainWindow {
public:
    static void Open(GtkApplication* app);

private:
    enum Page { kPageHost = 0,
        kPageClient = 1,
        kPageDevices = 2,
        kPageSettings = 3,
        kPageCount = 4 };

    enum class HostShareState { kIdle,
        kStarting,
        kSharing };

    enum class ShareTrigger { kUser,
        kAutomatic };

    static constexpr int kHostColumnCount = 8;

    struct HostRowWidgets {
        GtkWidget* cells[kHostColumnCount] = {};
        GtkWidget* action = nullptr;
        GtkWidget* attach = nullptr;
    };

    struct HostMonitor {
        std::string name;
        int x = 0;
        int y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    MainWindow() = default;
    ~MainWindow() = default;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void Build(GtkApplication* app);
    GtkWidget* BuildSidebar();
    GtkWidget* BuildHostPage();
    GtkWidget* BuildClientPage();
    GtkWidget* BuildDevicesPage();
    GtkWidget* BuildSettingsPage();
    void SelectPage(int page);

    void RefreshPairedDevices();
    void DrainPairingRequests();
    bool AskPairing(const PairingRequest& request);
    void ForgetSelectedDevice();
    void ForgetEveryDevice();

    bool Sharing() const;
    void StartTerminalShare();
    void StopTerminalShare();
    void StopTerminalRow();
    void RefreshShells();
    void KickShell(uint32_t termId);
    void StopAndAttachShell(uint32_t termId);
    void ApplySharingBanner();
    void OpenShell(const NetAddr& server, const std::string& passcode);

    void LoadSettings();
    void SaveSettings();
    void SaveRecentDevices();
    void PopulateBindCombo();
    void RebuildHostAddressRows();

    void StartScan();
    void RescanNow();
    void ScheduleRescan();
    void OnScanHit(const deskhubp::ScanHit& hit);
    void OnScanProgress(const deskhubp::ScanProgress& progress);
    void OnScanFinished(const deskhubp::ScanProgress& progress);

    void StartPoller();
    void OnDeviceStatus(const deskhubp::DeviceStatus& status);
    void RecordProbe(const std::string& addr, bool online, uint32_t rttMs);
    const deskhubp::DeviceStatus* ProbeFor(const std::string& addr) const;
    void RefreshDeviceList();
    void RefreshDeviceStatus();

    void ConnectWithPrompt(const std::string& addr, std::string passcode);
    void StartConnect(const std::string& addr, const std::string& passcode);
    struct OpenChoice {
        bool desktop = false;
        bool shell = false;
    };

    void OnSourcesReady(const std::string& addr, const std::string& passcode,
        const OpenChoice& choice, const deskhubp::ConnectOutcome& outcome);
    bool ReadPasscode(GtkWidget* entry, std::string& out);

    void OnShare(ShareTrigger trigger = ShareTrigger::kUser);
    void BeginAutoShare();
    static gboolean OnAutoShareTimer(gpointer user);
    void ReportShareProblem(const char* title, const std::string& text);
    static void OnMonitorsChanged(GdkScreen* screen, gpointer user);
    void StartHosting(const std::vector<AgentSource>& sources, const AgentOptions& options);
    void OnHostStarted(bool started, const std::string& error, const AgentOptions& options);
    void StopHosting();
    void UpdateHostRows(const std::vector<AgentSourceStatus>& rows);
    void ClearHostRows();
    void RebuildHostRowWidgets();
    HostRowWidgets MakeHostRowWidgets(const deskhub::ui::HostRow& ref, size_t index);
    void FillHostRow(const HostRowWidgets& widgets, const deskhub::ui::HostRowCells& cells);
    void RunRowAction(const deskhub::ui::HostRow& row);
    std::string HostPortDetail() const;
    void ApplyHostState(HostShareState state, const std::string& detail);
    void ShowIdleHostState();
    void RefreshDisplayChoices();
    void ShowHostTable(bool sharing);
    bool TerminalTicked() const;
    std::vector<HostMonitor> TickedMonitors() const;
    static std::vector<HostMonitor> ListMonitors();
    static bool MatchesTickedMonitor(const AgentSource& source,
        const std::vector<HostMonitor>& ticked);
    static std::vector<AgentSource> FilterToTickedMonitors(std::vector<AgentSource> sources,
        const std::vector<HostMonitor>& ticked);

    void ShowAfterSession();
    void SetBusy(bool busy, const char* what);
    void SetClientStatus(const std::string& text, bool isError);

    void ApplyTrayMode();
    bool EnsureTrayAttached();
    void ToggleWindowFromTray();
    void ShowMainWindow();

    uint16_t Port() const;
    void PostToUi(std::function<void()> fn);

    static void OnNavClicked(GtkButton* b, gpointer user);
    static void OnShareClicked(GtkButton* b, gpointer user);
    static void OnHostRowActionClicked(GtkButton* b, gpointer user);
    static void OnHostRowAttachClicked(GtkButton* b, gpointer user);
    static void OnConnectClicked(GtkButton* b, gpointer user);
    static void OnAddressActivate(GtkEntry* e, gpointer user);
    static void OnCopyClicked(GtkButton* b, gpointer user);
    static void OnSettingChanged(GtkWidget* w, gpointer user);
    static void OnBindChanged(GtkWidget* w, gpointer user);
    static void OnRefreshDevicesClicked(GtkButton* b, gpointer user);
    static void OnForgetDeviceClicked(GtkButton* b, gpointer user);
    static void OnForgetAllClicked(GtkButton* b, gpointer user);
    static void OnDeviceRowActivated(GtkTreeView* view, GtkTreePath* path,
        GtkTreeViewColumn* col, gpointer user);
    static gboolean OnRescanTimer(gpointer user);
    static gboolean OnHostTimer(gpointer user);
    static gboolean OnDeleteEvent(GtkWidget* w, GdkEvent* e, gpointer user);
    static void OnDestroy(GtkWidget* w, gpointer user);

    GtkWidget* window_ = nullptr;
    GtkWidget* stack_ = nullptr;
    GtkWidget* navButtons_[kPageCount] = {};

    GtkWidget* hostAddrBox_ = nullptr;
    GtkWidget* hostBanner_ = nullptr;
    GtkWidget* hostStateLabel_ = nullptr;
    GtkWidget* hostStatusLabel_ = nullptr;
    GtkWidget* hostHintLabel_ = nullptr;
    GtkWidget* hostPortalNote_ = nullptr;
    GtkWidget* hostGrid_ = nullptr;
    GtkWidget* hostGridFrame_ = nullptr;
    GtkWidget* hostPickerFrame_ = nullptr;
    GtkWidget* displayChecksBox_ = nullptr;
    std::vector<GtkWidget*> displayChecks_;
    std::vector<HostMonitor> availableDisplays_;
    std::vector<HostRowWidgets> hostRowWidgets_;
    GtkWidget* shareButton_ = nullptr;

    GtkWidget* hostTerminalCheck_ = nullptr;

    GtkWidget* addressEntry_ = nullptr;
    GtkWidget* portEntry_ = nullptr;
    GtkWidget* passcodeEntry_ = nullptr;
    GtkWidget* deviceNameEntry_ = nullptr;
    GtkWidget* connectButton_ = nullptr;
    GtkWidget* clientStatusLabel_ = nullptr;
    GtkWidget* controlCheck_ = nullptr;
    GtkWidget* desktopCheck_ = nullptr;
    GtkWidget* shellCheck_ = nullptr;

    GtkWidget* pairedView_ = nullptr;
    GtkListStore* pairedStore_ = nullptr;
    GtkWidget* pairedHintLabel_ = nullptr;
    GtkWidget* forgetDeviceButton_ = nullptr;
    GtkWidget* allowPairingCheck_ = nullptr;
    std::vector<deskhub::PairedDevice> pairedDevices_;
    GtkWidget* deviceHintLabel_ = nullptr;
    GtkListStore* deviceStore_ = nullptr;
    std::vector<deskhub::ui::DeviceRow> deviceRows_;

    GtkWidget* fpsSpin_ = nullptr;
    GtkWidget* bitrateSpin_ = nullptr;
    GtkWidget* portSpin_ = nullptr;
    GtkWidget* qualityCombo_ = nullptr;
    GtkWidget* hostPasscodeEntry_ = nullptr;
    GtkWidget* allowInputCheck_ = nullptr;
    GtkWidget* bindCombo_ = nullptr;
    GtkWidget* autoShareCheck_ = nullptr;
    GtkWidget* autostartCheck_ = nullptr;
    GtkWidget* startHiddenCheck_ = nullptr;
    GtkWidget* clipboardCheck_ = nullptr;
    GtkWidget* keepAwakeCheck_ = nullptr;
    TrayIcon tray_;
    guint clipTimerId_ = 0;

    static gboolean OnClipboardTimer(gpointer user);
    std::vector<std::string> bindChoices_;

    deskhub::ui::UiSettings settings_;
    std::vector<deskhub::ui::RecentDevice> recent_;
    std::vector<deskhubp::ScanHit> scanned_;
    std::vector<std::string> scannedThisRound_;
    std::map<uint64_t, deskhubp::DeviceStatus> probes_;
    std::vector<deskhub::ui::HostRow> hostRows_;

    deskhubp::LanScanner scanner_;
    deskhubp::DeviceStatusPoller poller_;
    AgentLoop agentLoop_;
    deskhubp::AgentDriver agentDriver_;
    deskhubp::TerminalHost terminalHost_;
    std::vector<deskhub::TerminalRecord> shells_;
    std::vector<AgentSourceStatus> hostStatus_;

    guint rescanTimerId_ = 0;
    guint hostTimerId_ = 0;
    guint autoShareTimerId_ = 0;
    deskhub::ui::AutoShareGate autoShareGate_;
    ShareTrigger shareTrigger_ = ShareTrigger::kUser;
    bool hosting_ = false;
    bool hostStarting_ = false;
    bool loadingSettings_ = false;
    bool askingPairing_ = false;
    bool screenSharing_ = false;
    bool terminalRequested_ = false;
    bool shareViewOnly_ = false;
    uint16_t sharePort_ = 0;
    std::string sharePasscodeNote_;
    std::string shareBindWarning_;

    deskhub::OpenViewerCount openViewers_;
    deskhubp::ConnectDriver connectDriver_;

    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
