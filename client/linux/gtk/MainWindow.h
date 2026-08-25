#pragma once
#include <gtk/gtk.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "TrayIcon.h"
#include "deskhub/session/OpenViewers.h"
#include "deskhub/ui/AutoShareGate.h"
#include "deskhub/ui/HostRows.h"
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
    void OnThemeChanged();

private:
    enum Page { kPageHost = 0,
        kPageClient = 1,
        kPageSettings = 2,
        kPageCount = 3 };

    enum class HostShareState { kIdle,
        kStarting,
        kSharing };

    enum class ShareTrigger { kUser,
        kAutomatic };

    static constexpr int kHostColumnCount = 8;

    struct HostRowWidgets {
        GtkWidget* cells[kHostColumnCount] = {};
        GtkWidget* action = nullptr;
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
    void SyncSessionCryptoControls();
    void RefreshSessionKeyDisplay();
    void SaveRecentDevices();
    void PopulateBindCombo();
    void RebuildHostAddressRows();
    void RefreshLogView();

    void StartScan();
    void RescanNow();
    void ScheduleRescan();
    void OnScanHit(const deskhubp::ScanHit& hit);
    void OnScanProgress(const deskhubp::ScanProgress& progress);
    void OnScanFinished(const deskhubp::ScanProgress& progress);
    void RefreshScanList();

    void StartPoller();
    void OnDeviceStatus(const deskhubp::DeviceStatus& status);
    void RecordProbe(const std::string& addr, bool online, uint32_t rttMs);
    const deskhubp::DeviceStatus* ProbeFor(const std::string& addr) const;
    void RefreshRecentList();
    void RefreshDeviceStatus();

    void ConnectWithPrompt(const std::string& addr, std::string passcode);
    void StartConnect(const std::string& addr, const std::string& passcode,
        const std::string& sessionKey);
    void OnSourcesReady(const std::string& addr, const std::string& passcode,
        const std::string& sessionKey, const deskhubp::ConnectOutcome& outcome);
    bool ReadPasscode(GtkWidget* entry, std::string& out);

    void OnShare(ShareTrigger trigger = ShareTrigger::kUser);
    void BeginAutoShare();
    static gboolean OnAutoShareTimer(gpointer user);
    void ReportShareProblem(const char* title, const std::string& text);
    static void OnMonitorsChanged(GdkScreen* screen, gpointer user);
    static bool MonitorsPresent();
    void StartHosting(const std::vector<AgentSource>& sources, const AgentOptions& options);
    void OnHostStarted(bool started, const std::string& error, const AgentOptions& options);
    void StopHosting();
    void UpdateHostRows(const std::vector<AgentSourceStatus>& rows);
    void ClearHostRows();
    void RebuildHostRowWidgets();
    HostRowWidgets MakeHostRowWidgets(const deskhub::ui::HostRow& ref, size_t index);
    void FillHostRow(const HostRowWidgets& widgets, const deskhub::ui::HostRow& ref,
        const AgentSourceStatus& status);
    void RunRowAction(const deskhub::ui::HostRow& row);
    std::string HostPortDetail() const;
    void ApplyHostState(HostShareState state, const std::string& detail);
    void ShowIdleHostState();

    void ShowAfterSession();
    void SetBusy(bool busy, const char* what);

    void ApplyTrayMode();
    void SyncHideTrayControl();
    void ToggleWindowFromTray();
    void ShowMainWindow();
    bool HasActiveSession() const;
    bool ConfirmQuitIfBusy();
    bool PromptBackgroundChoice();
    void HideToBackground();

    static gboolean OnCopyFeedbackTimer(gpointer user);
    void FlashCopyFeedback(GtkWidget* button, const char* restoreLabel);

    uint16_t Port() const;
    void PostToUi(std::function<void()> fn);

    static void OnNavClicked(GtkButton* b, gpointer user);
    static void OnShareClicked(GtkButton* b, gpointer user);
    static void OnHostRowActionClicked(GtkButton* b, gpointer user);
    static void OnConnectClicked(GtkButton* b, gpointer user);
    static void OnAddressActivate(GtkEntry* e, gpointer user);
    static void OnCopyClicked(GtkButton* b, gpointer user);
    static void OnSettingChanged(GtkWidget* w, gpointer user);
    static void OnEncryptToggled(GtkWidget* w, gpointer user);
    static void OnCopySessionKey(GtkButton* b, gpointer user);
    static void OnRefreshSessionKey(GtkButton* b, gpointer user);
    static void OnBindChanged(GtkWidget* w, gpointer user);
    static void OnLogFileChanged(GtkComboBox* c, gpointer user);
    static void OnLogRefreshClicked(GtkButton* b, gpointer user);
    static void OnLogOpenFolderClicked(GtkButton* b, gpointer user);
    static void OnLogDirBrowseClicked(GtkButton* b, gpointer user);
    static gboolean OnLogDirFocusOut(GtkWidget* w, GdkEventFocus* event, gpointer user);
    static void OnRescanClicked(GtkButton* b, gpointer user);
    static void OnRefreshStatusClicked(GtkButton* b, gpointer user);
    static void OnScanRowActivated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* col,
        gpointer user);
    static void OnRecentRowActivated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* col,
        gpointer user);
    static gboolean OnRecentButtonPress(GtkWidget* widget, GdkEventButton* event, gpointer user);
    static void OnForgetRecent(GtkMenuItem* item, gpointer user);
    void ForgetRecentAt(int row);
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
    GtkWidget* hostGrid_ = nullptr;
    std::vector<HostRowWidgets> hostRowWidgets_;
    GtkWidget* shareButton_ = nullptr;

    GtkWidget* addressEntry_ = nullptr;
    GtkWidget* portEntry_ = nullptr;
    GtkWidget* passcodeEntry_ = nullptr;
    GtkWidget* sessionKeyEntry_ = nullptr;
    GtkWidget* deviceNameEntry_ = nullptr;
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
    GtkWidget* languageCombo_ = nullptr;
    GtkWidget* hostPasscodeEntry_ = nullptr;
    GtkWidget* allowInputCheck_ = nullptr;
    GtkWidget* bindCombo_ = nullptr;
    GtkWidget* autoShareCheck_ = nullptr;
    GtkWidget* autostartCheck_ = nullptr;
    GtkWidget* runInBackgroundCheck_ = nullptr;
    GtkWidget* hideTrayIconCheck_ = nullptr;
    GtkWidget* clipboardCheck_ = nullptr;
    GtkWidget* shareAudioCheck_ = nullptr;
    GtkWidget* acceptFilesCheck_ = nullptr;
    GtkWidget* playAudioCheck_ = nullptr;
    GtkWidget* keepAwakeCheck_ = nullptr;
    GtkWidget* encryptSessionCheck_ = nullptr;
    GtkWidget* escrowSessionKeyCheck_ = nullptr;
    GtkWidget* sessionKeyLifetimeCombo_ = nullptr;
    GtkWidget* hostSessionKeyEntry_ = nullptr;
    GtkWidget* copySessionKeyBtn_ = nullptr;
    GtkWidget* refreshSessionKeyBtn_ = nullptr;
    GtkWidget* copyFeedbackBtn_ = nullptr;
    const char* copyFeedbackRestore_ = nullptr;
    guint copyFeedbackTimerId_ = 0;
    GtkWidget* escrowHint_ = nullptr;
    GtkWidget* sessionKeyHint_ = nullptr;
    GtkWidget* lifetimeLabel_ = nullptr;
    GtkWidget* sessionKeyLabel_ = nullptr;
    GtkWidget* sessionKeyBtnRow_ = nullptr;
    GtkWidget* logMaxFileMbSpin_ = nullptr;
    GtkWidget* logCompressDaysSpin_ = nullptr;
    GtkWidget* logDeleteDaysSpin_ = nullptr;
    GtkWidget* logDirEntry_ = nullptr;
    GtkWidget* logFileCombo_ = nullptr;
    GtkWidget* logViewText_ = nullptr;
    TrayIcon tray_;
    guint clipTimerId_ = 0;

    static gboolean OnClipboardTimer(gpointer user);
    std::vector<std::string> bindChoices_;
    std::vector<std::string> logPaths_;

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

    guint rescanTimerId_ = 0;
    guint hostTimerId_ = 0;
    guint autoShareTimerId_ = 0;
    deskhub::ui::AutoShareGate autoShareGate_;
    ShareTrigger shareTrigger_ = ShareTrigger::kUser;
    bool hosting_ = false;
    bool hostStarting_ = false;
    bool hostStopping_ = false;
    std::thread stopWorker_;
    bool loadingSettings_ = false;

    deskhub::OpenViewerCount openViewers_;
    deskhubp::ConnectDriver connectDriver_;

    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};
