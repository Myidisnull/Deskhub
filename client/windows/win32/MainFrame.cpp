#include <wx/dirdlg.h>
#include <wx/wx.h>

#include <wx/dcbuffer.h>
#include <wx/hyperlink.h>
#include <wx/init.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/clipbrd.h>
#include <wx/simplebook.h>
#include <wx/snglinst.h>
#include <wx/spinctrl.h>
#include <wx/taskbar.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "MainFrame.h"

#include "AppTheme.h"
#include "BackgroundPrompt.h"
#include "PasscodePrompt.h"
#include "QuitBusyPrompt.h"
#include "SourcePickerDialog.h"
#include "Viewer.h"
#include "deskhub/media/QualityPreset.h"
#include "deskhub/media/SourceLabel.h"
#include "deskhub/session/ShareFlow.h"
#include "deskhub/ui/Brand.h"
#include "deskhub/ui/HostRows.h"
#include "deskhub/ui/Locale.h"
#include "deskhub/crypto/KeyCodec.h"
#include "deskhub/ui/RecentDevices.h"
#include "deskhub/ui/Strings.h"
#include "deskhub/ui/UiSettings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/diag/StallLog.h"
#include "deskhubp/ffi/ClientFfi.h"
#include "deskhubp/media/DisplayEnum.h"
#include "deskhubp/net/DeviceStatusPoller.h"
#include "deskhubp/net/LanScanner.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/AgentDriver.h"
#include "deskhubp/session/AgentLoop.h"
#include "deskhubp/session/ConnectDriver.h"
#include "deskhub/crypto/KeyCodec.h"
#include "deskhubp/system/AppDataFile.h"
#include "deskhubp/system/Autostart.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/DeviceName.h"
#include "deskhubp/system/Language.h"
#include "deskhubp/system/SessionCrypto.h"
#include "deskhubp/system/UiSettingsStore.h"
#include "deskhubp/system/Random.h"

namespace {

namespace ui = deskhub::ui;
namespace brand = deskhub::brand;

constexpr const char* kRecentDevicesFile = "recent-devices.txt";

constexpr int kHostTimerId = 1;
constexpr int kScanTimerId = 2;
constexpr int kClipTimerId = 3;
constexpr int kCopyKeyFeedbackTimerId = 4;
constexpr int kRescanDelayMs = int(deskhubp::kLanRescanSecs) * 1000;
constexpr int kCopyKeyFeedbackMs = 1500;
constexpr int kHintWrapDip = 620;
constexpr int kPrimaryButtonH = 46;
constexpr int kConnectFieldW = 260;
constexpr int kSettingsValueW = 120;
constexpr int kPageMinClientW = 720;
constexpr int kPageMinClientH = 480;
constexpr int kLogViewH = 180;
constexpr int kListMinH = 110;
constexpr int kTrayRestoreId = wxID_HIGHEST + 1;
constexpr int kTrayExitId = wxID_HIGHEST + 2;

UINT SystemMonitorActivateMsg() {
    static const UINT msg = RegisterWindowMessageW(L"SystemMonitor.ActivateInstance");
    return msg;
}

enum Page { kPageHost = 0,
    kPageClient = 1,
    kPageSettings = 2,
    kPageCount = 3 };

const ui::LStr kPageLabels[kPageCount] = {ui::kSidebarHost, ui::kSidebarClient,
    ui::kSidebarSettings};

#define kAppBg (AppThemeCurrent().appBg)
#define kSidebarBg (AppThemeCurrent().sidebarBg)
#define kSidebarHover (AppThemeCurrent().sidebarHover)
#define kNavSelectedBg (AppThemeCurrent().navSelectedBg)
#define kAccent (AppThemeCurrent().accent)
#define kNavText (AppThemeCurrent().navText)
#define kSidebarFootnote (AppThemeCurrent().sidebarFootnote)
#define kHeadingText (AppThemeCurrent().headingText)
#define kMutedText (AppThemeCurrent().mutedText)
#define kOnline (AppThemeCurrent().online)
#define kOffline (AppThemeCurrent().offline)
#define kWarning (AppThemeCurrent().warning)
#define kRowLine (AppThemeCurrent().rowLine)
#define kViewerRowBg (AppThemeCurrent().viewerRowBg)
#define kBannerIdleBg (AppThemeCurrent().bannerIdleBg)
#define kBannerLiveBg (AppThemeCurrent().bannerLiveBg)
#define kBannerBusyBg (AppThemeCurrent().bannerBusyBg)
#define kPageBg (AppThemeCurrent().pageBg)
#define kSurfaceBg (AppThemeCurrent().surfaceBg)
#define kInputBg (AppThemeCurrent().inputBg)
#define kInputBorder (AppThemeCurrent().inputBorder)

constexpr const char* kTagPage = "dh.page";
constexpr const char* kTagSurface = "dh.surface";
constexpr const char* kTagHeading = "dh.heading";
constexpr const char* kTagMuted = "dh.muted";
constexpr const char* kTagSection = "dh.section";
constexpr const char* kTagBannerIdle = "dh.bannerIdle";
constexpr const char* kTagRowLine = "dh.rowLine";

void TintTagged(wxWindow* root) {
    if (!root) return;
    const wxString tag = root->GetName();
    if (tag == kTagPage)
        root->SetBackgroundColour(kPageBg);
    else if (tag == kTagSurface)
        root->SetBackgroundColour(kSurfaceBg);
    else if (tag == kTagHeading || tag == kTagSection)
        root->SetForegroundColour(kHeadingText);
    else if (tag == kTagMuted)
        root->SetForegroundColour(kMutedText);
    else if (tag == kTagBannerIdle)
        root->SetBackgroundColour(kBannerIdleBg);
    else if (tag == kTagRowLine)
        root->SetBackgroundColour(kRowLine);
    for (wxWindowList::compatibility_iterator node = root->GetChildren().GetFirst(); node;
        node = node->GetNext()) {
        TintTagged(node->GetData());
    }
}

enum class HostShareState { kIdle,
    kStarting,
    kSharing };

struct HostStateStyle {
    const char* label;
    const char* action;
    wxColour tint;
    wxColour background;
};

struct HostColumn {
    const char* title;
    int width;
    long align;
    bool mono;
};

constexpr int kHostColumnCount = 8;
constexpr int kHostActionWidth = 104;
constexpr int kHostRowHeight = 32;
constexpr int kHostRowBarWidth = 3;
constexpr int kHostCellGap = 8;

const HostColumn kHostColumns[kHostColumnCount] = {{"Source", 168, wxALIGN_LEFT, false},
    {"Size", 88, wxALIGN_LEFT, false}, {"Viewers", 58, wxALIGN_RIGHT, true},
    {"Client", 132, wxALIGN_LEFT, false}, {"Capture", 60, wxALIGN_RIGHT, true},
    {"Send", 52, wxALIGN_RIGHT, true}, {"Mbps", 56, wxALIGN_RIGHT, true},
    {"RTT", 54, wxALIGN_RIGHT, true}};

struct HostRowView {
    wxPanel* panel = nullptr;
    wxWindow* bar = nullptr;
    wxStaticText* cells[kHostColumnCount] = {};
};

wxFont MonoFont(const wxWindow* window) {
    wxFont font = window->GetFont();
    font.SetFamily(wxFONTFAMILY_TELETYPE);
    font.SetFaceName("Consolas");
    return font;
}

void PaintButton(wxButton* button, const wxColour& background) {
    button->SetBackgroundColour(background);
    button->SetForegroundColour(*wxWHITE);
    button->SetOwnBackgroundColour(background);
    button->SetOwnForegroundColour(*wxWHITE);
    button->SetFont(button->GetFont().Bold());
}

HostStateStyle StyleFor(HostShareState state) {
    switch (state) {
        case HostShareState::kSharing:
            return {ui::kShareStateOn, ui::kStopSharing, kOnline, kBannerLiveBg};
        case HostShareState::kStarting:
            return {ui::kStartingShare, ui::kStartSharing, kAccent, kBannerBusyBg};
        case HostShareState::kIdle: break;
    }
    return {ui::kShareStateOff, ui::kStartSharing, kMutedText, kBannerIdleBg};
}

wxString ToWx(const std::string& s) {
    return wxString::FromUTF8(s.c_str(), s.size());
}

wxString ToWx(const char* s) {
    return wxString::FromUTF8(s);
}

int64_t NowUnix() {
    return int64_t(std::time(nullptr));
}

std::string FormatLastConnected(int64_t unixTime) {
    if (unixTime <= 0) return {};
    const std::time_t t = std::time_t(unixTime);
    std::tm tm{};
    if (localtime_s(&tm, &t) != 0) return {};
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm) == 0) return {};
    return std::string(buf);
}

struct ProbeResult {
    bool online = false;
    uint32_t rttMs = 0;
};

bool HostKeyOf(const std::string& addr, uint64_t& key) {
    NetAddr parsed{};
    if (!ParseNetAddr(addr, parsed)) return false;
    key = parsed.Pack();
    return true;
}

bool SameHost(const std::string& addr, uint64_t key) {
    uint64_t other = 0;
    return HostKeyOf(addr, other) && other == key;
}

std::vector<std::string> AddressesOf(const std::vector<ui::RecentDevice>& devices) {
    std::vector<std::string> out;
    out.reserve(devices.size());
    for (const auto& d : devices) out.push_back(d.addr);
    return out;
}

wxStaticText* MakeHeading(wxWindow* parent, const char* text) {
    auto* heading = new wxStaticText(parent, wxID_ANY, ToWx(text));
    heading->SetName(kTagHeading);
    heading->SetFont(heading->GetFont().Bold().Scaled(1.35f));
    heading->SetForegroundColour(kHeadingText);
    return heading;
}

wxStaticText* MakeHint(wxWindow* parent, const wxString& text) {
    auto* hint = new wxStaticText(parent, wxID_ANY, text);
    hint->SetName(kTagMuted);
    hint->SetForegroundColour(kMutedText);
    hint->Wrap(parent->FromDIP(kHintWrapDip));
    return hint;
}

void SetHintLabel(wxStaticText* hint, const wxString& text) {
    hint->SetLabel(text);
    hint->Wrap(hint->FromDIP(kHintWrapDip));
    hint->GetParent()->Layout();
}

wxScrolledWindow* MakePageScroll(wxWindow* parent) {
    auto* scroll = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxVSCROLL | wxHSCROLL);
    scroll->SetName(kTagPage);
    scroll->SetBackgroundColour(kPageBg);
    scroll->SetScrollRate(parent->FromDIP(8), parent->FromDIP(8));
    scroll->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
    return scroll;
}

void FinishPageScroll(wxScrolledWindow* scroll, wxPanel* panel) {
    auto* scrollSizer = new wxBoxSizer(wxVERTICAL);
    scrollSizer->Add(panel, wxSizerFlags().Expand());
    scroll->SetSizer(scrollSizer);
    scroll->FitInside();
}

wxSizer* MakeHeadingRow(wxWindow* parent, const char* heading, const wxString& action,
    std::function<void()> onClick) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(MakeHeading(parent, heading), wxSizerFlags().CentreVertical());
    row->AddStretchSpacer(1);

    auto* button = new wxButton(parent, wxID_ANY, action);
    button->SetMinSize(parent->FromDIP(wxSize(110, 30)));
    button->Bind(wxEVT_BUTTON,
        [onClick = std::move(onClick)](wxCommandEvent&) { onClick(); });
    row->Add(button, wxSizerFlags().CentreVertical());
    return row;
}

wxStaticText* MakeSection(wxWindow* parent, const char* text) {
    auto* section = new wxStaticText(parent, wxID_ANY, ToWx(text));
    section->SetName(kTagSection);
    section->SetFont(section->GetFont().Bold().Scaled(1.1f));
    section->SetForegroundColour(kHeadingText);
    return section;
}

bool CopyTextToClipboard(HWND owner, const wxString& text) {
    const std::wstring wide = text.ToStdWstring();
    if (wide.empty() || !OpenClipboard(owner)) return false;
    EmptyClipboard();
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* mem = GlobalLock(handle)) {
            std::memcpy(mem, wide.c_str(), bytes);
            GlobalUnlock(handle);
            if (SetClipboardData(CF_UNICODETEXT, handle)) handle = nullptr;
        }
        if (handle) GlobalFree(handle);
    }
    CloseClipboard();
    return true;
}

class NavItem final : public wxWindow {
public:
    NavItem(wxWindow* parent, const wxString& label, std::function<void()> onClick)
        : wxWindow(parent, wxID_ANY), label_(label), onClick_(std::move(onClick)) {
        SetName("nav-" + label);
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(FromDIP(wxSize(160, 42)));
        SetCursor(wxCursor(wxCURSOR_HAND));
        SetFont(GetFont().Scaled(1.1f));
        Bind(wxEVT_PAINT, &NavItem::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            if (onClick_) onClick_();
        });
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) { SetHover(true); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { SetHover(false); });
    }

    void SetSelected(bool selected) {
        if (selected_ == selected) return;
        selected_ = selected;
        Refresh();
    }

private:
    void SetHover(bool hover) {
        if (hover_ == hover) return;
        hover_ = hover;
        Refresh();
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(kSidebarBg));
        dc.Clear();

        const wxRect rect = GetClientRect();
        const int inset = FromDIP(4);
        if (selected_ || hover_) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(selected_ ? kNavSelectedBg : kSidebarHover));
            dc.DrawRoundedRectangle(rect.x + inset, rect.y + FromDIP(2), rect.width - 2 * inset,
                rect.height - FromDIP(4), FromDIP(4));
        }
        if (selected_) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(kAccent));
            dc.DrawRoundedRectangle(rect.x + inset, rect.y + FromDIP(10), FromDIP(3),
                rect.height - FromDIP(20), FromDIP(1));
        }

        dc.SetFont(selected_ ? GetFont().Bold() : GetFont());
        dc.SetTextForeground(selected_ ? kHeadingText : kNavText);
        const wxSize extent = dc.GetTextExtent(label_);
        dc.DrawText(label_, FromDIP(18), (rect.GetHeight() - extent.GetHeight()) / 2);
    }

    wxString label_;
    std::function<void()> onClick_;
    bool selected_ = false;
    bool hover_ = false;
};

class MainFrame;

class MainFrame final : public wxFrame {
public:
    MainFrame();
    void RestoreFromTray();
    void RequestExit();

private:
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;

    wxWindow* BuildSidebar();
    wxWindow* BuildHostPage(wxWindow* parent);
    wxWindow* BuildClientPage(wxWindow* parent);
    wxWindow* BuildSettingsPage(wxWindow* parent);
    static wxTextCtrl* MakePasscodeCtrl(wxWindow* parent, int widthDip);

    void SelectPage(int page);
    void RefreshRecentList();
    void ApplyStatusToRow(long row, const std::string& addr);
    void ApplyScanPingToRow(long row, const std::string& addr);
    void ApplyProbeToRows(const std::string& addr);
    void RecordProbe(const std::string& addr, bool online, uint32_t rttMs);
    const ProbeResult* ProbeFor(const std::string& addr) const;
    void StartPoller();
    void OnDeviceStatus(const deskhubp::DeviceStatus& status);

    void OnShare();
    void StartHosting(const std::vector<AgentSource>& sources, const AgentOptions& options);
    void OnHostStarted(bool started, const std::string& error, uint16_t port,
        bool allowInput, const std::string& passcode);
    void StopHosting();
    void OnHostTimer(wxTimerEvent& event);
    void OnClipboardTimer(wxTimerEvent& event);
    void OnCopyKeyFeedbackTimer(wxTimerEvent& event);
    void FlashCopyFeedback(wxButton* button, const char* restoreLabel);
    void RefreshDisplayChoices();
    void UpdateHostRows(const std::vector<AgentSourceStatus>& rows);
    wxWindow* BuildHostTable(wxWindow* parent);
    wxButton* MakeRowAction(wxWindow* parent, const ui::HostRow& ref);
    void RebuildHostTable();
    void ShowHostTable(bool sharing);
    void RelayoutHostPage();
    void StopDisplay(uint8_t sourceId);
    void KickViewer(uint8_t sourceId, const std::string& viewerAddr);
    void ApplyHostState(HostShareState state, const wxString& detail);
    void ShowIdleHostState();
    std::string HostPortDetail() const;

    void StartConnect(const std::string& addr);
    void SetClientStatus(const wxString& text, const wxColour& colour);
    void ConnectWithPrompt(const std::string& addr, std::string passcode);
    void StartScan();
    void RescanNow();
    void RefreshDeviceStatus();
    void OnScanTimer(wxTimerEvent& event);
    void OnScanHit(const deskhubp::ScanHit& hit);
    void OnScanProgress(const deskhubp::ScanProgress& progress);
    void OnScanFinished(const deskhubp::ScanProgress& progress);
    void RefreshScanList();
    void OnListClick(wxListCtrl* list, wxMouseEvent& event, bool scanned);
    void OnRecentContextMenu(wxMouseEvent& event);
    void ForgetRecentAt(long row);
    void ConnectRow(long row, bool scanned);
    void OnSourcesReady(const std::string& addr, const std::string& passcode,
        const std::string& sessionKey, const deskhubp::ConnectOutcome& outcome);
    void OpenViewerSession(const std::string& addr, const std::string& passcode,
        const std::string& sessionKey, std::vector<deskhub::SourceInfo> picked);
    void SyncSessionCryptoControls();
    void RefreshSessionKeyDisplay();
    std::string ResolvedSessionKey(const std::string& addr) const;
    static bool IsValidSessionKeyHex(const std::string& hex);
    void DeselectAllRows();
    void SaveSettings();
    void PopulateBindChoice();
    void RebuildHostAddressRows();
    void SaveRecentDevices();
    void RefreshLogView();
    void OnClose(wxCloseEvent& event);
    void EnsureTray();
    void MinimizeToTray();
    void RemoveTray();
    void ApplyBackgroundSetting();
    void SyncHideTrayControl();
    void ApplyTheme();
    void OnSysColourChanged(wxSysColourChangedEvent& event);
    void Teardown();
    bool HasActiveSession() const;
    bool ConfirmQuitIfBusy();
    HostShareState CurrentHostShareState() const;

    wxPanel* sidebar_ = nullptr;
    wxStaticText* sidebarTitle_ = nullptr;
    wxHyperlinkCtrl* repoLink_ = nullptr;
    wxStaticText* versionLabel_ = nullptr;
    wxWindow* hostPage_ = nullptr;
    wxWindow* clientPage_ = nullptr;
    wxWindow* settingsPage_ = nullptr;
    wxSimplebook* book_ = nullptr;
    NavItem* pageButtons_[kPageCount] = {};
    wxTextCtrl* addrCtrl_ = nullptr;
    wxTextCtrl* connectPortCtrl_ = nullptr;
    wxButton* connectBtn_ = nullptr;
    wxStaticText* clientStatus_ = nullptr;
    wxListCtrl* scanList_ = nullptr;
    wxStaticText* scanStatus_ = nullptr;
    wxCheckBox* controlCtrl_ = nullptr;
    wxListCtrl* list_ = nullptr;
    wxStaticText* listHint_ = nullptr;
    wxPanel* hostAddrPanel_ = nullptr;
    wxPanel* hostBanner_ = nullptr;
    wxWindow* hostBannerBar_ = nullptr;
    wxStaticText* hostStateLabel_ = nullptr;
    wxStaticText* hostStatusLabel_ = nullptr;
    wxStaticText* hostHint_ = nullptr;
    wxListCtrl* hostPicker_ = nullptr;
    wxWindow* hostTableHolder_ = nullptr;
    wxScrolledWindow* hostTable_ = nullptr;
    std::vector<HostRowView> hostRowViews_;
    wxButton* shareBtn_ = nullptr;
    wxTextCtrl* clientPasscodeCtrl_ = nullptr;
    wxTextCtrl* clientSessionKeyCtrl_ = nullptr;
    wxTextCtrl* deviceNameCtrl_ = nullptr;
    wxSpinCtrl* fpsCtrl_ = nullptr;
    wxSpinCtrl* bitrateCtrl_ = nullptr;
    wxSpinCtrl* portCtrl_ = nullptr;
    wxChoice* qualityChoice_ = nullptr;
    wxChoice* languageChoice_ = nullptr;
    wxCheckBox* allowInputCtrl_ = nullptr;
    wxCheckBox* runInBackgroundCtrl_ = nullptr;
    wxCheckBox* hideTrayIconCtrl_ = nullptr;
    wxSpinCtrl* logMaxFileMbCtrl_ = nullptr;
    wxSpinCtrl* logCompressDaysCtrl_ = nullptr;
    wxSpinCtrl* logDeleteDaysCtrl_ = nullptr;
    wxTextCtrl* logDirCtrl_ = nullptr;
    wxChoice* logFileChoice_ = nullptr;
    wxTextCtrl* logViewCtrl_ = nullptr;
    wxTextCtrl* passcodeCtrl_ = nullptr;
    wxChoice* bindChoice_ = nullptr;
    wxCheckBox* autoShareCtrl_ = nullptr;
    wxCheckBox* autostartCtrl_ = nullptr;
    wxCheckBox* clipboardCtrl_ = nullptr;
    wxCheckBox* encryptSessionCtrl_ = nullptr;
    wxCheckBox* escrowSessionKeyCtrl_ = nullptr;
    wxChoice* sessionKeyLifetimeCtrl_ = nullptr;
    wxTextCtrl* sessionKeyCtrl_ = nullptr;
    wxButton* copySessionKeyBtn_ = nullptr;
    wxButton* refreshSessionKeyBtn_ = nullptr;
    wxStaticText* sessionKeyHint_ = nullptr;
    wxStaticText* escrowHint_ = nullptr;
    wxStaticText* lifetimeLabel_ = nullptr;
    wxStaticText* sessionKeyLabel_ = nullptr;
    wxWindow* sessionKeyRow_ = nullptr;
    wxTaskBarIcon* tray_ = nullptr;
    std::vector<std::string> bindChoices_;
    std::vector<std::string> logPaths_;

    deskhub::ui::UiSettings settings_;
    std::vector<AgentSource> availableDisplays_;
    std::vector<ui::HostRow> hostRows_;
    std::vector<ui::RecentDevice> recent_;
    std::vector<deskhubp::ScanHit> scanned_;
    std::vector<std::string> scannedThisRound_;
    std::map<uint64_t, ProbeResult> probes_;
    deskhubp::ConnectDriver connectDriver_;
    deskhubp::DeviceStatusPoller poller_;
    deskhubp::LanScanner scanner_;
    AgentLoop agentLoop_;
    deskhubp::AgentDriver agentDriver_;
    wxTimer hostTimer_;
    wxTimer scanTimer_;
    wxTimer clipTimer_;
    wxTimer copyKeyFeedbackTimer_;
    wxButton* copyFeedbackBtn_ = nullptr;
    const char* copyFeedbackRestore_ = ui::kCopySessionKey;
    bool hosting_ = false;
    bool hostStarting_ = false;
    bool prompting_ = false;
    bool quitting_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

class SystemMonitorTrayIcon final : public wxTaskBarIcon {
public:
    explicit SystemMonitorTrayIcon(MainFrame* frame)
        : frame_(frame) {
        Bind(wxEVT_TASKBAR_LEFT_UP, [this](wxTaskBarIconEvent&) { frame_->RestoreFromTray(); });
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { frame_->RestoreFromTray(); }, kTrayRestoreId);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { frame_->RequestExit(); }, kTrayExitId);
    }

    wxMenu* CreatePopupMenu() override {
        auto* menu = new wxMenu;
        menu->Append(kTrayRestoreId, ToWx(ui::kTrayRestore));
        menu->Append(kTrayExitId, ToWx(ui::kTrayExit));
        return menu;
    }

private:
    MainFrame* frame_;
};

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, ToWx(ui::kAppTitle)) {
    settings_ = deskhubp::LoadUiSettings();
    ui::ApplyUiLanguagePreference(settings_.language, deskhubp::SystemLanguageTag());
    SetTitle(ToWx(ui::kAppTitle));
    recent_ = ui::ParseRecentDevices(deskhubp::ReadAppDataFile(kRecentDevicesFile));

    auto* root = new wxBoxSizer(wxHORIZONTAL);
    root->Add(BuildSidebar(), wxSizerFlags().Expand());

    book_ = new wxSimplebook(this);
    hostPage_ = BuildHostPage(book_);
    clientPage_ = BuildClientPage(book_);
    settingsPage_ = BuildSettingsPage(book_);
    book_->AddPage(hostPage_, wxString());
    book_->AddPage(clientPage_, wxString());
    book_->AddPage(settingsPage_, wxString());
    root->Add(book_, wxSizerFlags(1).Expand());

    SetIcon(wxICON(deskhub_app_icon));

    SetSizer(root);
    SetMinClientSize(FromDIP(wxSize(kPageMinClientW, kPageMinClientH)));
    SetClientSize(FromDIP(wxSize(1140, 780)));
    Centre();

    hostTimer_.SetOwner(this, kHostTimerId);
    scanTimer_.SetOwner(this, kScanTimerId);
    clipTimer_.SetOwner(this, kClipTimerId);
    copyKeyFeedbackTimer_.SetOwner(this, kCopyKeyFeedbackTimerId);
    Bind(wxEVT_TIMER, &MainFrame::OnHostTimer, this, kHostTimerId);
    Bind(wxEVT_TIMER, &MainFrame::OnScanTimer, this, kScanTimerId);
    Bind(wxEVT_TIMER, &MainFrame::OnClipboardTimer, this, kClipTimerId);
    Bind(wxEVT_TIMER, &MainFrame::OnCopyKeyFeedbackTimer, this, kCopyKeyFeedbackTimerId);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    Bind(wxEVT_SYS_COLOUR_CHANGED, &MainFrame::OnSysColourChanged, this);

    RefreshRecentList();
    StartPoller();
    StartScan();
    SelectPage(kPageClient);
    ApplyTheme();
    ApplyBackgroundSetting();

    if (settings_.autoShare) {
        CallAfter([this, alive = alive_] {
            if (!*alive) return;
            SelectPage(kPageHost);
            OnShare();
        });
    }
}

WXLRESULT MainFrame::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) {
    if (nMsg == SystemMonitorActivateMsg()) {
        RestoreFromTray();
        return 0;
    }
    return wxFrame::MSWWindowProc(nMsg, wParam, lParam);
}

wxWindow* MainFrame::BuildSidebar() {
    sidebar_ = new wxPanel(this);
    sidebar_->SetBackgroundColour(kSidebarBg);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sidebarTitle_ = new wxStaticText(sidebar_, wxID_ANY, ToWx(ui::kAppTitle));
    sidebarTitle_->SetName(kTagHeading);
    sidebarTitle_->SetFont(sidebarTitle_->GetFont().Bold().Scaled(1.6f));
    sidebarTitle_->SetForegroundColour(kHeadingText);
    sizer->Add(sidebarTitle_, wxSizerFlags().Border(wxALL, FromDIP(16)));

    for (int i = 0; i < kPageCount; ++i) {
        auto* item = new NavItem(sidebar_, ToWx(kPageLabels[i]), [this, i] { SelectPage(i); });
        sizer->Add(item, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10)));
        pageButtons_[i] = item;
    }

    sizer->AddStretchSpacer(1);

    repoLink_ = new wxHyperlinkCtrl(sidebar_, wxID_ANY, ToWx(ui::kProjectLinkLabel),
        ToWx(ui::kProjectUrl));
    repoLink_->SetBackgroundColour(kSidebarBg);
    repoLink_->SetNormalColour(kNavText);
    repoLink_->SetVisitedColour(kNavText);
    repoLink_->SetHoverColour(kAccent);
    repoLink_->SetToolTip(ToWx(ui::kProjectUrl));
    sizer->Add(repoLink_, wxSizerFlags().Border(wxLEFT | wxRIGHT, FromDIP(16)));

    versionLabel_ = new wxStaticText(sidebar_, wxID_ANY, ToWx(ui::VersionLine()));
    versionLabel_->SetForegroundColour(kSidebarFootnote);
    sizer->Add(versionLabel_,
        wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM | wxTOP, FromDIP(16)));

    sidebar_->SetSizer(sizer);
    return sidebar_;
}

wxWindow* MainFrame::BuildHostPage(wxWindow* parent) {
    auto* scroll = MakePageScroll(parent);
    auto* panel = new wxPanel(scroll);
    panel->SetName(kTagPage);
    panel->SetBackgroundColour(kPageBg);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    const wxSizerFlags pad = wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    sizer->Add(MakeHeading(panel, ui::kHostHeading), pad);
    sizer->Add(MakeHint(panel, ToWx(ui::kHostIpIntro)), pad);

    auto* netRow = new wxBoxSizer(wxHORIZONTAL);
    netRow->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kBindInterfaceLabel)),
        wxSizerFlags().CentreVertical());
    bindChoice_ = new wxChoice(panel, wxID_ANY);
    PopulateBindChoice();
    bindChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        SaveSettings();
        RebuildHostAddressRows();
    });
    netRow->Add(bindChoice_, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(14)));
    sizer->Add(netRow, pad);

    hostAddrPanel_ = new wxPanel(panel);
    sizer->Add(hostAddrPanel_,
        wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(14)));
    RebuildHostAddressRows();

    hostBanner_ = new wxPanel(panel);
    auto* bannerRow = new wxBoxSizer(wxHORIZONTAL);
    hostBannerBar_ = new wxWindow(hostBanner_, wxID_ANY, wxDefaultPosition,
        FromDIP(wxSize(4, -1)));
    bannerRow->Add(hostBannerBar_, wxSizerFlags().Expand());

    auto* bannerText = new wxBoxSizer(wxVERTICAL);
    hostStateLabel_ = new wxStaticText(hostBanner_, wxID_ANY, wxString());
    hostStateLabel_->SetFont(hostStateLabel_->GetFont().Bold().Scaled(1.1f));
    bannerText->Add(hostStateLabel_, wxSizerFlags().Border(wxBOTTOM, FromDIP(4)));
    hostStatusLabel_ = new wxStaticText(hostBanner_, wxID_ANY, wxString());
    hostStatusLabel_->SetForegroundColour(kMutedText);
    bannerText->Add(hostStatusLabel_);
    bannerRow->Add(bannerText, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(10)));

    hostBanner_->SetSizer(bannerRow);
    sizer->Add(hostBanner_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    hostPicker_ = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL);
    hostPicker_->SetBackgroundColour(kSurfaceBg);
    hostPicker_->SetForegroundColour(kHeadingText);
    hostPicker_->InsertColumn(0, "Source", wxLIST_FORMAT_LEFT, FromDIP(560));
    hostPicker_->SetMinSize(FromDIP(wxSize(-1, kListMinH)));
    sizer->Add(hostPicker_,
        wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(14)));

    hostTableHolder_ = BuildHostTable(panel);
    hostTableHolder_->SetMinSize(FromDIP(wxSize(-1, kListMinH)));
    sizer->Add(hostTableHolder_,
        wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(14)));

    hostHint_ = MakeHint(panel, ToWx(ui::kPickDisplaysHint));
    sizer->Add(hostHint_, pad);

    shareBtn_ = new wxButton(panel, wxID_ANY, wxString());
    shareBtn_->SetMinSize(FromDIP(wxSize(-1, kPrimaryButtonH)));
    shareBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnShare(); });
    sizer->Add(shareBtn_, wxSizerFlags().Expand().Border(wxALL, FromDIP(14)));

    panel->SetSizer(sizer);
    FinishPageScroll(scroll, panel);
    ShowIdleHostState();
    RefreshDisplayChoices();
    return scroll;
}

void MainFrame::PopulateBindChoice() {
    bindChoice_->Clear();
    bindChoices_.clear();
    bindChoices_.push_back("");
    bindChoice_->Append(ToWx(ui::kBindAllInterfaces));
    int active = 0;
    for (const AdapterAddr& adapter : ListLocalIPv4()) {
        bindChoice_->Append(ToWx(adapter.ip + "  (" + adapter.name + ")"));
        bindChoices_.push_back(adapter.ip);
        if (adapter.ip == settings_.bindIp) active = int(bindChoices_.size() - 1);
    }
    if (!settings_.bindIp.empty() && active == 0) {
        bindChoice_->Append(
            ToWx(settings_.bindIp + "  (" + std::string(ui::kBindNotConnectedNote) + ")"));
        bindChoices_.push_back(settings_.bindIp);
        active = int(bindChoices_.size() - 1);
    }
    bindChoice_->SetSelection(active);
}

void MainFrame::RebuildHostAddressRows() {
    if (copyFeedbackBtn_ && copyFeedbackBtn_->GetParent() == hostAddrPanel_) {
        copyKeyFeedbackTimer_.Stop();
        copyFeedbackBtn_ = nullptr;
    }
    hostAddrPanel_->DestroyChildren();
    auto* holder = new wxBoxSizer(wxVERTICAL);
    std::vector<AdapterAddr> shown;
    for (const auto& a : ListLocalIPv4())
        if (settings_.bindIp.empty() || a.ip == settings_.bindIp) shown.push_back(a);
    if (shown.empty()) {
        const std::string text = settings_.bindIp.empty()
                                     ? std::string(ui::kNoNetworkAddress)
                                     : settings_.bindIp + "  (" + std::string(ui::kBindNotConnectedNote) + ")";
        holder->Add(new wxStaticText(hostAddrPanel_, wxID_ANY, ToWx(text)));
    } else {
        auto* grid = new wxFlexGridSizer(3, FromDIP(wxSize(14, 10)));
        grid->AddGrowableCol(1, 1);
        for (const auto& a : shown) {
            grid->Add(new wxStaticText(hostAddrPanel_, wxID_ANY, ToWx(a.name)),
                wxSizerFlags().CentreVertical());
            auto* ipText = new wxStaticText(hostAddrPanel_, wxID_ANY, ToWx(a.ip));
            ipText->SetFont(ipText->GetFont().Bold());
            grid->Add(ipText, wxSizerFlags().CentreVertical());
            auto* copy = new wxButton(hostAddrPanel_, wxID_ANY, ToWx(ui::kCopy));
            copy->SetMinSize(FromDIP(wxSize(84, 32)));
            const wxString ip = ToWx(a.ip);
            copy->Bind(wxEVT_BUTTON, [this, ip, copy](wxCommandEvent&) {
                if (!CopyTextToClipboard(HWND(GetHandle()), ip)) return;
                FlashCopyFeedback(copy, ui::kCopy);
            });
            grid->Add(copy);
        }
        holder->Add(grid, wxSizerFlags(1).Expand());
    }
    hostAddrPanel_->SetSizer(holder);
    hostAddrPanel_->GetParent()->Layout();
}

wxTextCtrl* MainFrame::MakePasscodeCtrl(wxWindow* parent, int widthDip) {
    auto* ctrl = new wxTextCtrl(parent, wxID_ANY, wxString(), wxDefaultPosition,
        parent->FromDIP(wxSize(widthDip, -1)), wxTE_PROCESS_ENTER,
        wxTextValidator(wxFILTER_DIGITS));
    ctrl->SetMaxLength(deskhub::kPasscodeDigits);
    return ctrl;
}

wxWindow* MainFrame::BuildClientPage(wxWindow* parent) {
    auto* scroll = MakePageScroll(parent);
    auto* panel = new wxPanel(scroll);
    panel->SetName(kTagPage);
    panel->SetBackgroundColour(kPageBg);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    const wxSizerFlags pad = wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    sizer->Add(MakeHeading(panel, ui::kClientHeading), pad);

    auto connectNow = [this](wxCommandEvent&) {
        const uint16_t port =
            ui::PortOrDefault(std::string(connectPortCtrl_->GetValue().utf8_str()));
        StartConnect(ui::AddressWithPort(std::string(addrCtrl_->GetValue().utf8_str()), port));
    };

    auto* grid = new wxFlexGridSizer(2, FromDIP(wxSize(12, 12)));

    grid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kClientIpPrompt)),
        wxSizerFlags().CentreVertical());
    addrCtrl_ = new wxTextCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition,
        FromDIP(wxSize(kConnectFieldW, -1)), wxTE_PROCESS_ENTER);
    addrCtrl_->SetName("address-field");
    addrCtrl_->SetHint(ToWx(ui::kClientIpPlaceholder));
    addrCtrl_->Bind(wxEVT_TEXT_ENTER, connectNow);
    grid->Add(addrCtrl_, wxSizerFlags().CentreVertical());

    grid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kUdpPortLabel)),
        wxSizerFlags().CentreVertical());
    connectPortCtrl_ = new wxTextCtrl(panel, wxID_ANY,
        ToWx(std::to_string(deskhub::kDeskhubPort)), wxDefaultPosition,
        FromDIP(wxSize(kConnectFieldW, -1)), wxTE_PROCESS_ENTER);
    connectPortCtrl_->Bind(wxEVT_TEXT_ENTER, connectNow);
    grid->Add(connectPortCtrl_, wxSizerFlags().CentreVertical());

    grid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kClientPasscodePrompt)),
        wxSizerFlags().CentreVertical());
    clientPasscodeCtrl_ = MakePasscodeCtrl(panel, kConnectFieldW);
    clientPasscodeCtrl_->SetName("passcode-field");
    clientPasscodeCtrl_->SetToolTip(ToWx(ui::kClientPasscodeHint));
    clientPasscodeCtrl_->Bind(wxEVT_TEXT_ENTER, connectNow);
    grid->Add(clientPasscodeCtrl_, wxSizerFlags().CentreVertical());

    grid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kClientSessionKeyPrompt)),
        wxSizerFlags().CentreVertical());
    clientSessionKeyCtrl_ = new wxTextCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition,
        FromDIP(wxSize(kConnectFieldW, -1)), wxTE_PROCESS_ENTER);
    clientSessionKeyCtrl_->SetName("session-key-field");
    clientSessionKeyCtrl_->SetToolTip(ToWx(ui::kClientSessionKeyHint));
    clientSessionKeyCtrl_->Bind(wxEVT_TEXT_ENTER, connectNow);
    grid->Add(clientSessionKeyCtrl_, wxSizerFlags().CentreVertical());

    grid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kDeviceNameLabel)),
        wxSizerFlags().CentreVertical());
    const std::string initialName =
        settings_.deviceName.empty() ? deskhubp::LocalDeviceName() : settings_.deviceName;
    deviceNameCtrl_ = new wxTextCtrl(panel, wxID_ANY, ToWx(initialName), wxDefaultPosition,
        FromDIP(wxSize(260, -1)), wxTE_PROCESS_ENTER);
    deviceNameCtrl_->SetName("name-field");
    deviceNameCtrl_->Bind(wxEVT_TEXT_ENTER, connectNow);
    grid->Add(deviceNameCtrl_, wxSizerFlags().CentreVertical());

    sizer->Add(grid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    connectBtn_ = new wxButton(panel, wxID_ANY, "Connect");
    connectBtn_->SetName("connect-button");
    connectBtn_->SetMinSize(FromDIP(wxSize(-1, kPrimaryButtonH)));
    PaintButton(connectBtn_, kAccent);
    connectBtn_->Bind(wxEVT_BUTTON, connectNow);
    sizer->Add(connectBtn_,
        wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    clientStatus_ = new wxStaticText(panel, wxID_ANY, wxString());
    clientStatus_->SetForegroundColour(kMutedText);
    sizer->Add(clientStatus_, wxSizerFlags().Centre().Border(wxTOP, FromDIP(8)));

    controlCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kRequestControlLabel));
    controlCtrl_->SetValue(settings_.clientControl);
    controlCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        settings_.clientControl = controlCtrl_->GetValue();
        deskhubp::SaveUiSettings(settings_);
    });
    sizer->Add(controlCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(20)));

    sizer->Add(MakeHeadingRow(panel, ui::kLanDevicesHeading, ToWx(ui::kRefreshNow),
                   [this] { RescanNow(); }),
        wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    scanList_ = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL);
    scanList_->SetBackgroundColour(kSurfaceBg);
    scanList_->SetForegroundColour(kHeadingText);
    scanList_->InsertColumn(0, "Device", wxLIST_FORMAT_LEFT, FromDIP(170));
    scanList_->InsertColumn(1, "Ping", wxLIST_FORMAT_RIGHT, FromDIP(70));
    scanList_->SetMinSize(FromDIP(wxSize(-1, kListMinH)));
    scanList_->Bind(wxEVT_LEFT_DOWN,
        [this](wxMouseEvent& event) { OnListClick(scanList_, event, true); });
    sizer->Add(scanList_,
        wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    scanStatus_ = MakeHint(panel, ToWx(ui::kLanDevicesEmpty));
    sizer->Add(scanStatus_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    sizer->Add(MakeHeadingRow(panel, ui::kRecentDevicesHeading, ToWx(ui::kRefreshNow),
                   [this] { RefreshDeviceStatus(); }),
        wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    list_ = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->SetBackgroundColour(kSurfaceBg);
    list_->SetForegroundColour(kHeadingText);
    list_->InsertColumn(0, "Device", wxLIST_FORMAT_LEFT, FromDIP(170));
    list_->InsertColumn(1, "Status", wxLIST_FORMAT_LEFT, FromDIP(100));
    list_->InsertColumn(2, "Ping", wxLIST_FORMAT_RIGHT, FromDIP(70));
    list_->InsertColumn(3, "Last connected", wxLIST_FORMAT_LEFT, FromDIP(150));
    list_->SetMinSize(FromDIP(wxSize(-1, kListMinH)));
    list_->Bind(wxEVT_LEFT_DOWN,
        [this](wxMouseEvent& event) { OnListClick(list_, event, false); });
    list_->Bind(wxEVT_RIGHT_DOWN,
        [this](wxMouseEvent& event) { OnRecentContextMenu(event); });
    sizer->Add(list_, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    listHint_ = MakeHint(panel, ToWx(ui::kRecentDevicesEmpty));
    sizer->Add(listHint_, wxSizerFlags().Border(wxALL, FromDIP(16)));

    panel->SetSizer(sizer);
    FinishPageScroll(scroll, panel);
    return scroll;
}

wxWindow* MainFrame::BuildSettingsPage(wxWindow* parent) {
    auto* scroll = MakePageScroll(parent);

    auto* panel = new wxPanel(scroll);
    panel->SetName(kTagPage);
    panel->SetBackgroundColour(kPageBg);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    const wxSizerFlags pad = wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    sizer->Add(MakeHeading(panel, ui::kSettingsHeading), pad);
    sizer->Add(MakeHint(panel, ToWx(ui::kSettingsHint)), pad);

    const wxSize valueSize = FromDIP(wxSize(kSettingsValueW, -1));

    sizer->AddSpacer(FromDIP(8));
    sizer->Add(MakeSection(panel, ui::kSettingsSectionLanguage), pad);
    auto* langGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));
    langGrid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kLanguageLabel)),
        wxSizerFlags().CentreVertical());
    languageChoice_ = new wxChoice(panel, wxID_ANY, wxDefaultPosition, valueSize);
    int languageSel = 0;
    const ui::UiLanguage preferred = ui::ParseLanguageCode(settings_.language);
    for (size_t i = 0; i < ui::kLanguageOptionCount; ++i) {
        const ui::LanguageOption& opt = ui::LanguageOptions()[i];
        const char* label =
            opt.language == ui::UiLanguage::System ? ui::kLanguageSystem.get() : opt.nativeName;
        languageChoice_->Append(ToWx(label));
        if (opt.language == preferred) languageSel = int(i);
    }
    languageChoice_->SetSelection(languageSel);
    langGrid->Add(languageChoice_);
    sizer->Add(langGrid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    sizer->Add(MakeHint(panel, ToWx(ui::kLanguageRestartHint)), pad);

    sizer->AddSpacer(FromDIP(8));
    sizer->Add(MakeSection(panel, ui::kSettingsSectionVideo), pad);
    auto* videoGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));

    videoGrid->Add(new wxStaticText(panel, wxID_ANY, "FPS"), wxSizerFlags().CentreVertical());
    fpsCtrl_ = new wxSpinCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition, valueSize,
        wxSP_ARROW_KEYS, 1, int(ui::kMaxSettingsFps), int(settings_.fps));
    videoGrid->Add(fpsCtrl_);

    videoGrid->Add(new wxStaticText(panel, wxID_ANY, "Bitrate (Mbps)"),
        wxSizerFlags().CentreVertical());
    bitrateCtrl_ = new wxSpinCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition, valueSize,
        wxSP_ARROW_KEYS, 1, int(ui::kMaxSettingsBitrateMbps), int(settings_.bitrateMbps));
    videoGrid->Add(bitrateCtrl_);

    videoGrid->Add(new wxStaticText(panel, wxID_ANY, "Quality"),
        wxSizerFlags().CentreVertical());
    qualityChoice_ = new wxChoice(panel, wxID_ANY, wxDefaultPosition, valueSize);
    for (const auto& preset : deskhub::media::kQualityPresets)
        qualityChoice_->Append(ToWx(preset.label));
    qualityChoice_->SetSelection(int(deskhub::media::QualityPresetIndex(settings_.maxDim)));
    videoGrid->Add(qualityChoice_);

    sizer->Add(videoGrid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    sizer->AddSpacer(FromDIP(12));
    sizer->Add(MakeSection(panel, ui::kSettingsSectionConnection), pad);
    auto* netGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));

    netGrid->Add(new wxStaticText(panel, wxID_ANY, "UDP port"),
        wxSizerFlags().CentreVertical());
    portCtrl_ = new wxSpinCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition, valueSize,
        wxSP_ARROW_KEYS, 1, int(ui::kMaxSettingsPort), int(settings_.port));
    netGrid->Add(portCtrl_);

    sizer->Add(netGrid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    sizer->AddSpacer(FromDIP(12));
    sizer->Add(MakeSection(panel, ui::kSettingsSectionSecurity), pad);
    auto* securityGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));
    securityGrid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kPasscodeLabel)),
        wxSizerFlags().CentreVertical());
    passcodeCtrl_ = MakePasscodeCtrl(panel, kSettingsValueW);
    passcodeCtrl_->SetValue(ToWx(settings_.passcode));
    securityGrid->Add(passcodeCtrl_);
    sizer->Add(securityGrid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    allowInputCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kAllowControlLabel));
    allowInputCtrl_->SetValue(settings_.allowInput);
    sizer->Add(allowInputCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    sizer->AddSpacer(FromDIP(12));
    sizer->Add(MakeSection(panel, ui::kSettingsSectionSession), pad);
    clipboardCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kClipboardSyncLabel));
    clipboardCtrl_->SetValue(settings_.clipboardSync);
    sizer->Add(clipboardCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    encryptSessionCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kEncryptSessionLabel));
    encryptSessionCtrl_->SetValue(settings_.encryptSession);
    sizer->Add(encryptSessionCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    sizer->Add(MakeHint(panel, ToWx(ui::kEncryptSessionHint)), pad);

    escrowSessionKeyCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kEscrowSessionKeyLabel));
    escrowSessionKeyCtrl_->SetValue(settings_.escrowSessionKey);
    sizer->Add(escrowSessionKeyCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    escrowHint_ = MakeHint(panel, ToWx(ui::kEscrowSessionKeyHint));
    sizer->Add(escrowHint_, pad);

    auto* lifetimeGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));
    lifetimeLabel_ = new wxStaticText(panel, wxID_ANY, ToWx(ui::kSessionKeyLifetimeLabel));
    lifetimeGrid->Add(lifetimeLabel_, wxSizerFlags().CentreVertical());
    sessionKeyLifetimeCtrl_ = new wxChoice(panel, wxID_ANY);
    sessionKeyLifetimeCtrl_->Append(ToWx(ui::kSessionKeyLifetimePerShare));
    sessionKeyLifetimeCtrl_->Append(ToWx(ui::kSessionKeyLifetimePersistent));
    sessionKeyLifetimeCtrl_->SetSelection(
        settings_.sessionKeyLifetime == ui::SessionKeyLifetime::Persistent ? 1 : 0);
    lifetimeGrid->Add(sessionKeyLifetimeCtrl_);
    sizer->Add(lifetimeGrid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    auto* keyGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));
    sessionKeyLabel_ = new wxStaticText(panel, wxID_ANY, ToWx(ui::kSessionKeyLabel));
    keyGrid->Add(sessionKeyLabel_, wxSizerFlags().CentreVertical());
    sessionKeyCtrl_ = new wxTextCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition,
        FromDIP(wxSize(kConnectFieldW, -1)), wxTE_READONLY);
    keyGrid->Add(sessionKeyCtrl_);
    sizer->Add(keyGrid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    sessionKeyHint_ = MakeHint(panel, ToWx(ui::kSessionKeyHint));
    sizer->Add(sessionKeyHint_, pad);

    sessionKeyRow_ = new wxPanel(panel);
    sessionKeyRow_->SetName(kTagPage);
    auto* keyBtns = new wxBoxSizer(wxHORIZONTAL);
    copySessionKeyBtn_ = new wxButton(sessionKeyRow_, wxID_ANY, ToWx(ui::kCopySessionKey));
    refreshSessionKeyBtn_ = new wxButton(sessionKeyRow_, wxID_ANY, ToWx(ui::kRefreshSessionKey));
    keyBtns->Add(copySessionKeyBtn_);
    keyBtns->AddSpacer(FromDIP(8));
    keyBtns->Add(refreshSessionKeyBtn_);
    sessionKeyRow_->SetSizer(keyBtns);
    sizer->Add(sessionKeyRow_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    SyncSessionCryptoControls();

    sizer->AddSpacer(FromDIP(12));
    sizer->Add(MakeSection(panel, ui::kSettingsSectionLaunch), pad);
    autostartCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kAutostartLabel));
    autostartCtrl_->SetValue(deskhubp::AutostartEnabled());
    sizer->Add(autostartCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    autoShareCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kShareOnLaunchLabel));
    autoShareCtrl_->SetValue(settings_.autoShare);
    sizer->Add(autoShareCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    runInBackgroundCtrl_ =
        new wxCheckBox(panel, wxID_ANY, ToWx(ui::kRunInBackgroundLabel));
    runInBackgroundCtrl_->SetValue(settings_.runInBackground);
    sizer->Add(runInBackgroundCtrl_,
        wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    hideTrayIconCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kHideTrayIconLabel));
    hideTrayIconCtrl_->SetValue(settings_.hideTrayIcon);
    sizer->Add(hideTrayIconCtrl_,
        wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    SyncHideTrayControl();

    sizer->AddSpacer(FromDIP(12));
    sizer->Add(MakeSection(panel, "Logs"), pad);
    auto* logGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));

    logGrid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kLogMaxFileMbLabel)),
        wxSizerFlags().CentreVertical());
    logMaxFileMbCtrl_ = new wxSpinCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition, valueSize,
        wxSP_ARROW_KEYS, int(deskhub::diag::kMinLogMaxFileMb),
        int(deskhub::diag::kMaxLogMaxFileMb), int(settings_.logMaxFileMb));
    logGrid->Add(logMaxFileMbCtrl_);

    logGrid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kLogCompressAfterDaysLabel)),
        wxSizerFlags().CentreVertical());
    logCompressDaysCtrl_ = new wxSpinCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition, valueSize,
        wxSP_ARROW_KEYS, 0, int(deskhub::diag::kMaxLogRetentionDays),
        int(settings_.logCompressAfterDays));
    logGrid->Add(logCompressDaysCtrl_);

    logGrid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kLogDeleteAfterDaysLabel)),
        wxSizerFlags().CentreVertical());
    logDeleteDaysCtrl_ = new wxSpinCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition, valueSize,
        wxSP_ARROW_KEYS, 0, int(deskhub::diag::kMaxLogRetentionDays),
        int(settings_.logDeleteAfterDays));
    logGrid->Add(logDeleteDaysCtrl_);

    sizer->Add(logGrid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    sizer->Add(MakeHint(panel, ToWx(ui::kLogDirHint)), pad);

    auto* logDirGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));
    logDirGrid->AddGrowableCol(1, 1);
    logDirGrid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kLogDirLabel)),
        wxSizerFlags().CentreVertical());
    auto* logDirRow = new wxBoxSizer(wxHORIZONTAL);
    logDirCtrl_ = new wxTextCtrl(panel, wxID_ANY, ToWx(settings_.logDir), wxDefaultPosition,
        FromDIP(wxSize(360, -1)), wxTE_PROCESS_ENTER);
    logDirCtrl_->SetHint(ToWx(deskhubp::ConfigDir()));
    logDirRow->Add(logDirCtrl_, wxSizerFlags(1).CentreVertical());
    auto* browseLogDir = new wxButton(panel, wxID_ANY, ToWx(ui::kLogDirBrowse));
    browseLogDir->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        wxDirDialog dlg(this, ToWx(ui::kLogDirLabel),
            logDirCtrl_->GetValue().empty() ? ToWx(deskhubp::ConfigDir()) : logDirCtrl_->GetValue());
        if (dlg.ShowModal() != wxID_OK) return;
        logDirCtrl_->ChangeValue(dlg.GetPath());
        SaveSettings();
        RefreshLogView();
    });
    logDirRow->Add(browseLogDir, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(8)));
    logDirGrid->Add(logDirRow, wxSizerFlags().Expand());
    sizer->Add(logDirGrid, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    sizer->AddSpacer(FromDIP(8));
    sizer->Add(MakeHint(panel, ToWx(ui::kLogDetailsLabel)), pad);
    auto* logTools = new wxBoxSizer(wxHORIZONTAL);
    logFileChoice_ = new wxChoice(panel, wxID_ANY);
    logTools->Add(logFileChoice_, wxSizerFlags(1).CentreVertical());
    auto* refreshLogs = new wxButton(panel, wxID_ANY, ToWx(ui::kLogRefresh));
    logTools->Add(refreshLogs, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(8)));
    auto* openLogs = new wxButton(panel, wxID_ANY, ToWx(ui::kLogOpenFolder));
    logTools->Add(openLogs, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(8)));
    sizer->Add(logTools, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    logViewCtrl_ = new wxTextCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition,
        FromDIP(wxSize(-1, kLogViewH)), wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP | wxHSCROLL);
    logViewCtrl_->SetFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE));
    logViewCtrl_->SetMinSize(FromDIP(wxSize(-1, kLogViewH)));
    sizer->Add(logViewCtrl_,
        wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(16)));

    fpsCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    fpsCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    bitrateCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    bitrateCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    portCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    portCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    qualityChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SaveSettings(); });
    languageChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SaveSettings(); });
    allowInputCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
    clipboardCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
    encryptSessionCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        SaveSettings();
        SyncSessionCryptoControls();
    });
    escrowSessionKeyCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
    sessionKeyLifetimeCtrl_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SaveSettings(); });
    copySessionKeyBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!sessionKeyCtrl_ || !copySessionKeyBtn_) return;
        if (!CopyTextToClipboard(HWND(GetHandle()), sessionKeyCtrl_->GetValue())) return;
        FlashCopyFeedback(copySessionKeyBtn_, ui::kCopySessionKey);
    });
    refreshSessionKeyBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!deskhubp::EnsureSessionKeyMaterial(settings_, true)) return;
        RefreshSessionKeyDisplay();
        SaveSettings();
    });
    autoShareCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
    autostartCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
    runInBackgroundCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        settings_.runInBackground = runInBackgroundCtrl_->GetValue();
        settings_.runInBackgroundChoiceMade = true;
        if (!settings_.runInBackground) {
            settings_.hideTrayIcon = false;
            hideTrayIconCtrl_->SetValue(false);
        }
        SyncHideTrayControl();
        SaveSettings();
        ApplyBackgroundSetting();
    });
    hideTrayIconCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        settings_.hideTrayIcon = hideTrayIconCtrl_->GetValue();
        SaveSettings();
        ApplyBackgroundSetting();
    });
    passcodeCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    logDirCtrl_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
        SaveSettings();
        RefreshLogView();
    });
    logDirCtrl_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        SaveSettings();
        RefreshLogView();
        event.Skip();
    });
    logMaxFileMbCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    logMaxFileMbCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    logCompressDaysCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    logCompressDaysCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    logDeleteDaysCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    logDeleteDaysCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    logFileChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshLogView(); });
    refreshLogs->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RefreshLogView(); });
    openLogs->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { deskhubp::OpenLogFolder(); });

    panel->SetSizer(sizer);
    FinishPageScroll(scroll, panel);
    RefreshLogView();
    return scroll;
}

void MainFrame::SelectPage(int page) {
    for (int i = 0; i < kPageCount; ++i) pageButtons_[i]->SetSelected(i == page);
    book_->ChangeSelection(size_t(page));
    if (auto* scroll = dynamic_cast<wxScrolledWindow*>(book_->GetCurrentPage()))
        scroll->FitInside();
    if (page == kPageHost && !hosting_ && !hostStarting_) RefreshDisplayChoices();
    if (page == kPageSettings) RefreshLogView();
}

void MainFrame::RefreshDisplayChoices() {
    availableDisplays_ = deskhubp::ListDisplays();
    hostRows_.clear();
    RebuildHostTable();
    hostPicker_->DeleteAllItems();
    hostPicker_->EnableCheckBoxes(true);
    for (size_t i = 0; i < availableDisplays_.size(); ++i) {
        const AgentSource& source = availableDisplays_[i];
        const long row = hostPicker_->InsertItem(long(i),
            ToWx(deskhub::media::SourcePickerLabel(source.name, uint8_t(i), source.width,
                source.height)));
        hostPicker_->CheckItem(row, true);
    }
    ShowHostTable(false);
}

wxWindow* MainFrame::BuildHostTable(wxWindow* parent) {
    auto* card = new wxPanel(parent);
    card->SetName(kTagRowLine);
    card->SetBackgroundColour(kRowLine);
    auto* cardSizer = new wxBoxSizer(wxVERTICAL);

    auto* holder = new wxPanel(card);
    holder->SetName(kTagSurface);
    holder->SetBackgroundColour(kSurfaceBg);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxPanel(holder);
    header->SetName(kTagBannerIdle);
    header->SetBackgroundColour(kBannerIdleBg);
    header->SetMinSize(FromDIP(wxSize(-1, 30)));
    auto* headerRow = new wxBoxSizer(wxHORIZONTAL);
    headerRow->AddSpacer(FromDIP(kHostRowBarWidth + kHostCellGap));
    for (const HostColumn& column : kHostColumns) {
        auto* title = new wxStaticText(header, wxID_ANY, ToWx(column.title).Upper(),
            wxDefaultPosition, FromDIP(wxSize(column.width, -1)), column.align);
        title->SetName(kTagMuted);
        title->SetForegroundColour(kMutedText);
        title->SetFont(title->GetFont().Bold().Scaled(0.85f));
        headerRow->Add(title, wxSizerFlags().CentreVertical().Border(wxRIGHT,
                                  FromDIP(kHostCellGap)));
    }
    headerRow->AddSpacer(FromDIP(kHostActionWidth));
    header->SetSizer(headerRow);
    sizer->Add(header, wxSizerFlags().Expand());

    auto* headerLine = new wxWindow(holder, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 1)));
    headerLine->SetName(kTagRowLine);
    headerLine->SetBackgroundColour(kRowLine);
    sizer->Add(headerLine, wxSizerFlags().Expand());

    hostTable_ = new wxScrolledWindow(holder);
    hostTable_->SetName(kTagSurface);
    hostTable_->SetBackgroundColour(kSurfaceBg);
    hostTable_->SetScrollRate(FromDIP(8), FromDIP(8));
    hostTable_->SetSizer(new wxBoxSizer(wxVERTICAL));
    sizer->Add(hostTable_, wxSizerFlags(1).Expand());

    holder->SetSizer(sizer);
    cardSizer->Add(holder, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(1)));
    card->SetSizer(cardSizer);
    return card;
}

wxButton* MainFrame::MakeRowAction(wxWindow* parent, const ui::HostRow& ref) {
    const bool viewer = ref.viewer;
    auto* button = new wxButton(parent, wxID_ANY,
        ToWx(viewer ? ui::kDisconnectViewerAction : ui::kStopDisplayAction));
    button->SetMinSize(FromDIP(wxSize(kHostActionWidth, 26)));
    PaintButton(button, viewer ? kWarning : kOffline);

    const uint8_t sourceId = ref.sourceId;
    const std::string addr = ref.viewerAddr;
    button->Bind(wxEVT_BUTTON, [this, viewer, sourceId, addr](wxCommandEvent&) {
        if (viewer) {
            KickViewer(sourceId, addr);
        } else {
            StopDisplay(sourceId);
        }
    });
    return button;
}

void MainFrame::RebuildHostTable() {
    hostRowViews_.clear();
    wxSizer* rows = hostTable_->GetSizer();
    rows->Clear(true);

    for (const ui::HostRow& ref : hostRows_) {
        if (!ref.viewer && !hostRowViews_.empty()) {
            auto* line = new wxWindow(hostTable_, wxID_ANY, wxDefaultPosition,
                FromDIP(wxSize(-1, 1)));
            line->SetBackgroundColour(kRowLine);
            rows->Add(line, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, FromDIP(4)));
        }

        HostRowView view;
        view.panel = new wxPanel(hostTable_);
        view.panel->SetBackgroundColour(ref.viewer ? kViewerRowBg : kSurfaceBg);
        view.panel->SetMinSize(FromDIP(wxSize(-1, kHostRowHeight)));

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        view.bar = new wxWindow(view.panel, wxID_ANY, wxDefaultPosition,
            FromDIP(wxSize(kHostRowBarWidth, -1)));
        row->Add(view.bar, wxSizerFlags().Expand());
        row->AddSpacer(FromDIP(kHostCellGap));

        for (int c = 0; c < kHostColumnCount; ++c) {
            const HostColumn& column = kHostColumns[c];
            view.cells[c] = new wxStaticText(view.panel, wxID_ANY, wxString(), wxDefaultPosition,
                FromDIP(wxSize(column.width, -1)), column.align);
            if (column.mono) view.cells[c]->SetFont(MonoFont(view.cells[c]));
            if (c == 0 && !ref.viewer)
                view.cells[c]->SetFont(view.cells[c]->GetFont().Bold());
            row->Add(view.cells[c], wxSizerFlags().CentreVertical().Border(wxRIGHT,
                                        FromDIP(kHostCellGap)));
        }
        row->Add(MakeRowAction(view.panel, ref), wxSizerFlags().CentreVertical());
        row->AddSpacer(FromDIP(kHostCellGap));
        view.panel->SetSizer(row);

        rows->Add(view.panel, wxSizerFlags().Expand());
        hostRowViews_.push_back(view);
    }

    hostTable_->FitInside();
    hostTable_->Layout();
}

void MainFrame::ShowHostTable(bool sharing) {
    hostPicker_->Show(!sharing);
    hostTableHolder_->Show(sharing);
    hostHint_->Show(!sharing);
    RelayoutHostPage();
}

void MainFrame::RefreshRecentList() {
    list_->DeleteAllItems();
    for (size_t i = 0; i < recent_.size(); ++i) {
        const auto& device = recent_[i];
        const long row = list_->InsertItem(long(i), ToWx(device.addr));
        list_->SetItem(row, 3, ToWx(FormatLastConnected(device.lastConnectedUnix)));
        ApplyStatusToRow(row, device.addr);
    }
    SetHintLabel(listHint_,
        ToWx(ui::RecentDevicesNote(recent_.size(), deskhubp::kDeviceStatusRoundSecs)));
}

const ProbeResult* MainFrame::ProbeFor(const std::string& addr) const {
    uint64_t key = 0;
    if (!HostKeyOf(addr, key)) return nullptr;
    const auto it = probes_.find(key);
    return it == probes_.end() ? nullptr : &it->second;
}

void MainFrame::RecordProbe(const std::string& addr, bool online, uint32_t rttMs) {
    uint64_t key = 0;
    if (!HostKeyOf(addr, key)) return;
    probes_[key] = ProbeResult{online, rttMs};
}

void MainFrame::ApplyStatusToRow(long row, const std::string& addr) {
    const ProbeResult* probe = ProbeFor(addr);
    if (!probe) {
        list_->SetItem(row, 1, ToWx(ui::kStatusChecking));
        list_->SetItem(row, 2, "-");
        list_->SetItemTextColour(row, kMutedText);
        return;
    }
    list_->SetItem(row, 1, ToWx(probe->online ? ui::kStatusOnline : ui::kStatusOffline));
    list_->SetItem(row, 2, probe->online ? ToWx(ui::PingMs(probe->rttMs)) : wxString("-"));
    list_->SetItemTextColour(row, probe->online ? kOnline : kOffline);
}

void MainFrame::ApplyScanPingToRow(long row, const std::string& addr) {
    const ProbeResult* probe = ProbeFor(addr);
    const bool online = !probe || probe->online;
    scanList_->SetItem(row, 1, online && probe ? ToWx(ui::PingMs(probe->rttMs)) : wxString("-"));
    scanList_->SetItemTextColour(row, online ? kOnline : kOffline);
}

void MainFrame::ApplyProbeToRows(const std::string& addr) {
    uint64_t key = 0;
    if (!HostKeyOf(addr, key)) return;
    for (size_t i = 0; i < recent_.size(); ++i)
        if (SameHost(recent_[i].addr, key)) ApplyStatusToRow(long(i), recent_[i].addr);
    for (size_t i = 0; i < scanned_.size(); ++i)
        if (SameHost(scanned_[i].addr, key)) ApplyScanPingToRow(long(i), scanned_[i].addr);
}

void MainFrame::StartPoller() {
    poller_.SetAddresses(AddressesOf(recent_));
    poller_.Start([this](const deskhubp::DeviceStatus& status) {
        CallAfter([this, status] { OnDeviceStatus(status); });
    });
}

void MainFrame::OnDeviceStatus(const deskhubp::DeviceStatus& status) {
    RecordProbe(status.addr, status.online, status.rttMs);
    ApplyProbeToRows(status.addr);
}

std::string MainFrame::HostPortDetail() const {
    return ui::UdpPortLine(uint16_t(settings_.port)) + ".";
}

void MainFrame::ApplyHostState(HostShareState state, const wxString& detail) {
    const HostStateStyle style = StyleFor(state);

    hostStateLabel_->SetLabel(ToWx(style.label));
    hostStateLabel_->SetForegroundColour(style.tint);
    hostStateLabel_->SetBackgroundColour(style.background);
    hostStatusLabel_->SetLabel(detail);
    hostStatusLabel_->SetBackgroundColour(style.background);
    hostBannerBar_->SetBackgroundColour(style.tint);
    hostBanner_->SetBackgroundColour(style.background);
    hostBanner_->Refresh();

    shareBtn_->SetLabel(ToWx(style.action));
    PaintButton(shareBtn_, state == HostShareState::kSharing ? kOffline : kAccent);
    shareBtn_->Refresh();

    bindChoice_->Enable(state == HostShareState::kIdle);
}

void MainFrame::ShowIdleHostState() {
    ApplyHostState(HostShareState::kIdle, ToWx(HostPortDetail()));
}

void MainFrame::OnShare() {
    if (hostStarting_) return;
    if (hosting_) {
        LOGI("[UI] Share stop requested.");
        StopHosting();
        return;
    }

    if (availableDisplays_.empty()) {
        const std::string err = deskhubp::ListDisplaysError();
        wxMessageBox(err.empty() ? wxString("No display found to share.") : ToWx(err),
            ToWx(ui::kAppTitle), wxOK | wxICON_WARNING, this);
        return;
    }

    std::vector<AgentSource> chosen;
    for (size_t i = 0; i < availableDisplays_.size(); ++i) {
        if (long(i) >= hostPicker_->GetItemCount()) break;
        if (hostPicker_->IsItemChecked(long(i))) chosen.push_back(availableDisplays_[i]);
    }
    if (chosen.empty()) {
        wxMessageBox(ToWx(ui::kNoDisplayTicked), ToWx(ui::kAppTitle), wxOK | wxICON_WARNING, this);
        return;
    }

    const deskhub::ShareClampResult clamp = deskhub::ClampShareSources(chosen);
    if (clamp.clamped)
        wxMessageBox(ToWx(ui::ShareClampWarning()), ToWx(ui::kAppTitle), wxOK | wxICON_WARNING, this);

    const std::vector<AgentSource>& sources = clamp.sources;

    AgentOptions options;
    options.fps = settings_.fps;
    options.bitrateMbps = settings_.bitrateMbps;
    options.maxDim = settings_.maxDim;
    options.port = uint16_t(settings_.port);
    options.allowInput = settings_.allowInput;
    options.passcode = settings_.passcode;
    options.bindIp = settings_.bindIp;
    options.clipboardSync = settings_.clipboardSync;
    if (!deskhubp::ApplyEncryptToAgentOptions(settings_, options)) {
        wxMessageBox(ToWx(ui::kShareStartFailed), ToWx(ui::kAppTitle), wxOK | wxICON_ERROR, this);
        return;
    }
    SaveSettings();

    LOGI("[UI] Share start: %zu source(s), %u fps, %u Mbps, port %u.", sources.size(), options.fps,
        options.bitrateMbps, unsigned(options.port));
    StartHosting(sources, options);
}

void MainFrame::StartHosting(const std::vector<AgentSource>& sources,
    const AgentOptions& options) {
    hostStarting_ = true;
    shareBtn_->Disable();
    ApplyHostState(HostShareState::kStarting, ToWx(HostPortDetail()));
    hostRows_.clear();
    RebuildHostTable();
    ShowHostTable(true);

    agentDriver_.Join();
    agentDriver_.StartAsync(
        agentLoop_, sources, options,
        [alive = alive_](std::function<void()> fn) {
            if (!wxTheApp) return;
            wxTheApp->CallAfter([alive, fn = std::move(fn)] {
                if (*alive) fn();
            });
        },
        [this, port = options.port, allowInput = options.allowInput,
            passcode = options.passcode](bool started, const std::string& error) {
            OnHostStarted(started, error, port, allowInput, passcode);
        });
}

void MainFrame::OnHostStarted(bool started, const std::string& error, uint16_t port,
    bool allowInput, const std::string& passcode) {
    hostStarting_ = false;
    shareBtn_->Enable();

    if (!started) {
        ShowIdleHostState();
        RefreshDisplayChoices();
        wxMessageBox(ToWx(std::string(ui::kShareStartFailed) + ".\n\n" + error), ToWx(ui::kAppTitle),
            wxOK | wxICON_ERROR, this);
        return;
    }

    hosting_ = true;
    std::string status = ui::SharingStatusLine(port);
    if (!passcode.empty()) status += " " + ui::PasscodeNote(passcode);
    if (!allowInput) status += std::string(" ") + ui::kViewOnlyNote.get();
    const std::string bindWarning = agentLoop_.BindWarning();
    if (!bindWarning.empty()) status += " " + bindWarning;
    ApplyHostState(HostShareState::kSharing, ToWx(status));
    ShowHostTable(true);
    hostTimer_.Start(int(deskhubp::kAgentStatusPollMs));
    if (settings_.clipboardSync) clipTimer_.Start(1000);
}

void MainFrame::OnClipboardTimer(wxTimerEvent&) {
    if (!hosting_) return;
    if (const auto remote = agentLoop_.TakeRemoteClipboard()) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(*remote)));
            wxTheClipboard->Close();
        }
        return;
    }
    if (!wxTheClipboard->Open()) return;
    if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT)) {
        wxTextDataObject data;
        wxTheClipboard->GetData(data);
        const std::string text(data.GetText().utf8_str());
        if (!text.empty()) agentLoop_.OfferLocalClipboard(text);
    }
    wxTheClipboard->Close();
}

void MainFrame::FlashCopyFeedback(wxButton* button, const char* restoreLabel) {
    if (!button || !restoreLabel) return;
    if (copyFeedbackBtn_ && copyFeedbackBtn_ != button) {
        copyFeedbackBtn_->SetLabel(ToWx(copyFeedbackRestore_));
    }
    copyFeedbackBtn_ = button;
    copyFeedbackRestore_ = restoreLabel;
    button->SetLabel(ToWx(ui::kCopied));
    copyKeyFeedbackTimer_.StartOnce(kCopyKeyFeedbackMs);
}

void MainFrame::OnCopyKeyFeedbackTimer(wxTimerEvent&) {
    if (!copyFeedbackBtn_) return;
    copyFeedbackBtn_->SetLabel(ToWx(copyFeedbackRestore_));
    copyFeedbackBtn_ = nullptr;
}

void MainFrame::StopHosting() {
    hostTimer_.Stop();
    clipTimer_.Stop();
    const uint64_t t0 = NowUs();
    deskhubp::StopAnrWatch watch("ui", "stop_hosting");
    LOGI("[DIAG][ui] evt=stop_begin phase=stop_hosting");
    agentLoop_.Stop();
    agentDriver_.Join();
    deskhubp::LogStopPhase("ui", "stop_hosting", t0);
    hosting_ = false;
    ShowIdleHostState();
    RefreshDisplayChoices();
}

void MainFrame::OnHostTimer(wxTimerEvent&) {
    if (!hosting_) return;

    std::vector<AgentSourceStatus> rows;
    const deskhubp::AgentDriveState state = agentDriver_.Poll(agentLoop_, rows);
    if (state == deskhubp::AgentDriveState::Stopped) {
        StopHosting();
        return;
    }
    if (state == deskhubp::AgentDriveState::Running) UpdateHostRows(rows);
}

void MainFrame::UpdateHostRows(const std::vector<AgentSourceStatus>& rows) {
    std::vector<ui::HostRow> refs = ui::BuildHostRows(rows);

    if (refs != hostRows_) {
        hostRows_ = std::move(refs);
        RebuildHostTable();
        RelayoutHostPage();
    }

    for (size_t i = 0; i < hostRows_.size() && i < hostRowViews_.size(); ++i) {
        const ui::HostRow& ref = hostRows_[i];
        const AgentSourceStatus* s = ui::FindHostSource(rows, ref.sourceId);
        if (!s) continue;

        const ui::HostRowCells cells = ui::HostRowText(ref, *s);
        const wxString texts[kHostColumnCount] = {ToWx(cells.source), ToWx(cells.size),
            ToWx(cells.viewers), ToWx(cells.client), ToWx(cells.capture), ToWx(cells.send),
            ToWx(cells.mbps), ToWx(cells.rtt)};
        const HostRowView& view = hostRowViews_[i];
        const wxColour colour = cells.online ? kHeadingText : kMutedText;
        view.bar->SetBackgroundColour(cells.online ? kOnline : kRowLine);
        view.bar->Refresh();

        for (int c = 0; c < kHostColumnCount; ++c) {
            wxStaticText* cell = view.cells[c];
            if (cell->GetLabel() != texts[c]) cell->SetLabel(texts[c]);
            cell->SetForegroundColour(colour);
        }
    }
}

void MainFrame::RelayoutHostPage() {
    hostPicker_->GetParent()->Layout();
    if (auto* scroll = dynamic_cast<wxScrolledWindow*>(hostPage_)) scroll->FitInside();
}

void MainFrame::StopDisplay(uint8_t sourceId) {
    if (!hosting_) return;
    agentLoop_.StopSource(sourceId);
}

void MainFrame::KickViewer(uint8_t sourceId, const std::string& viewerAddr) {
    if (!hosting_) return;
    NetAddr addr{};
    if (!ParseNetAddr(viewerAddr, addr)) return;
    agentLoop_.KickViewer(sourceId, addr.Pack());
}

void MainFrame::SetClientStatus(const wxString& text, const wxColour& colour) {
    clientStatus_->SetLabel(text);
    clientStatus_->SetForegroundColour(colour);
    clientStatus_->Wrap(FromDIP(kHintWrapDip));
    clientStatus_->GetParent()->Layout();
}

void MainFrame::StartConnect(const std::string& rawAddr) {
    LOGI("[UI] Connect requested for \"%s\".", rawAddr.c_str());
    SetClientStatus(wxString(), kMutedText);
    std::string deviceName =
        ui::TruncateDeviceName(std::string(deviceNameCtrl_->GetValue().utf8_str()));
    if (deviceName.empty()) deviceName = deskhubp::LocalDeviceName();
    deviceNameCtrl_->ChangeValue(ToWx(deviceName));
    if (deviceName != settings_.deviceName) {
        settings_.deviceName = deviceName;
        deskhubp::SaveUiSettings(settings_);
    }
    const std::string addr = ui::TrimAscii(rawAddr);
    LOGI("[Connect] Clicked for \"%s\".", addr.c_str());
    if (addr.empty()) {
        wxMessageBox("Enter the host machine's IP address first (e.g., 192.168.1.10).",
            ToWx(ui::kAppTitle), wxOK | wxICON_WARNING, this);
        return;
    }

    NetAddr server{};
    if (!ParseNetAddr(addr, server)) {
        wxMessageBox(ToWx("Invalid address: \"" + addr + "\"\n" + ui::InvalidAddressHint()),
            ToWx(ui::kAppTitle), wxOK | wxICON_ERROR, this);
        return;
    }

    const std::string typed(clientPasscodeCtrl_->GetValue().utf8_str());
    const std::string passcode = deskhub::IsValidPasscode(typed)
                                     ? typed
                                     : ui::PasscodeForDevice(recent_, addr);
    if (!deskhub::IsValidPasscode(passcode)) {
        wxMessageBox(ToWx(ui::kPasscodeInvalid), ToWx(ui::kAppTitle), wxOK | wxICON_ERROR, this);
        clientPasscodeCtrl_->SetFocus();
        return;
    }

    const std::string sessionKey = ResolvedSessionKey(addr);
    if (!sessionKey.empty() && !IsValidSessionKeyHex(sessionKey)) {
        wxMessageBox(ToWx(ui::kSessionKeyInvalid), ToWx(ui::kAppTitle), wxOK | wxICON_ERROR, this);
        clientSessionKeyCtrl_->SetFocus();
        return;
    }
    if (ui::EncryptedForDevice(recent_, addr) && sessionKey.empty()) {
        wxMessageBox(ToWx(ui::kSessionKeyInvalid), ToWx(ui::kAppTitle), wxOK | wxICON_WARNING,
            this);
        clientSessionKeyCtrl_->SetFocus();
        return;
    }

    const bool started = connectDriver_.QueryAsync(
        server, passcode,
        [alive = alive_](std::function<void()> fn) {
            if (!wxTheApp) return;
            wxTheApp->CallAfter([alive, fn = std::move(fn)] {
                if (*alive) fn();
            });
        },
        [this, addr, passcode, sessionKey](const deskhubp::ConnectOutcome& outcome) {
            OnSourcesReady(addr, passcode, sessionKey, outcome);
        });
    if (!started) {
        SetClientStatus(ToWx(ui::kConnectInProgress), kMutedText);
        return;
    }
    connectBtn_->Disable();
    SetClientStatus(ToWx(ui::kQueryingSources), kMutedText);
    Raise();
}

void MainFrame::StartScan() {
    scannedThisRound_.clear();
    const bool started = scanner_.Start(
        uint16_t(settings_.port),
        [alive = alive_](std::function<void()> fn) {
            if (!wxTheApp) return;
            wxTheApp->CallAfter([alive, fn = std::move(fn)] {
                if (*alive) fn();
            });
        },
        [this](const deskhubp::ScanHit& hit) { OnScanHit(hit); },
        [this](const deskhubp::ScanProgress& progress) { OnScanProgress(progress); },
        [this](const deskhubp::ScanProgress& progress) { OnScanFinished(progress); });
    if (!started) scanTimer_.StartOnce(kRescanDelayMs);
}

void MainFrame::RescanNow() {
    scanTimer_.Stop();
    SetHintLabel(scanStatus_, ToWx(ui::kLanDevicesEmpty));
    StartScan();
}

void MainFrame::RefreshDeviceStatus() {
    for (const ui::RecentDevice& device : recent_) {
        uint64_t key = 0;
        if (HostKeyOf(device.addr, key)) probes_.erase(key);
    }
    RefreshRecentList();
    poller_.RefreshNow();
}

void MainFrame::OnScanTimer(wxTimerEvent&) {
    StartScan();
}

void MainFrame::OnScanHit(const deskhubp::ScanHit& hit) {
    scannedThisRound_.push_back(hit.addr);
    RecordProbe(hit.addr, true, hit.rttMs);

    const auto known = std::find_if(scanned_.begin(), scanned_.end(),
        [&hit](const deskhubp::ScanHit& seen) { return seen.addr == hit.addr; });
    if (known == scanned_.end()) {
        scanned_.push_back(hit);
        RefreshScanList();
    } else {
        known->rttMs = hit.rttMs;
    }
    ApplyProbeToRows(hit.addr);
}

void MainFrame::OnScanProgress(const deskhubp::ScanProgress& progress) {
    SetHintLabel(scanStatus_,
        ToWx(ui::ScanningStatus(progress.probed, progress.total, uint16_t(settings_.port))));
}

void MainFrame::OnScanFinished(const deskhubp::ScanProgress& progress) {
    const auto gone = [this](const deskhubp::ScanHit& hit) {
        return std::find(scannedThisRound_.begin(), scannedThisRound_.end(), hit.addr) ==
               scannedThisRound_.end();
    };
    scanned_.erase(std::remove_if(scanned_.begin(), scanned_.end(), gone), scanned_.end());
    RefreshScanList();

    SetHintLabel(scanStatus_,
        ToWx(ui::LanDevicesNote(scanned_.size(), progress.total, deskhubp::kLanRescanSecs)));
    scanTimer_.StartOnce(kRescanDelayMs);
}

void MainFrame::RefreshScanList() {
    scanList_->DeleteAllItems();
    for (size_t i = 0; i < scanned_.size(); ++i) {
        const long row = scanList_->InsertItem(long(i), ToWx(scanned_[i].addr));
        ApplyScanPingToRow(row, scanned_[i].addr);
    }
}

void MainFrame::OnListClick(wxListCtrl* list, wxMouseEvent& event, bool scanned) {
    event.Skip();
    int flags = 0;
    const long row = list->HitTest(event.GetPosition(), flags);
    LOGI("[UI] %s list click: row %ld%s.", scanned ? "LAN" : "Recent", row,
        prompting_ ? " (prompt already open)" : "");

    if (row == wxNOT_FOUND || prompting_) return;

    prompting_ = true;
    CallAfter([this, row, scanned] {
        ConnectRow(row, scanned);
        prompting_ = false;
    });
}

void MainFrame::OnRecentContextMenu(wxMouseEvent& event) {
    int flags = 0;
    const long row = list_->HitTest(event.GetPosition(), flags);
    event.Skip();
    if (row == wxNOT_FOUND || size_t(row) >= recent_.size()) return;
    list_->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    wxMenu menu;
    menu.Append(wxID_DELETE, ToWx(ui::kForgetDevice));
    menu.Bind(wxEVT_MENU, [this, row](wxCommandEvent&) { ForgetRecentAt(row); }, wxID_DELETE);
    PopupMenu(&menu);
}

void MainFrame::ForgetRecentAt(long row) {
    if (row < 0 || size_t(row) >= recent_.size()) return;
    const std::string addr = recent_[size_t(row)].addr;
    ui::RemoveRecentDevice(recent_, addr);
    deskhubp::WriteAppDataFile(kRecentDevicesFile, ui::SerializeRecentDevices(recent_));
    poller_.SetAddresses(AddressesOf(recent_));
    RefreshRecentList();
}

void MainFrame::ConnectRow(long row, bool scanned) {
    if (row < 0) return;
    if (scanned) {
        if (size_t(row) >= scanned_.size()) return;
        const std::string addr = scanned_[size_t(row)].addr;
        ConnectWithPrompt(addr, ui::PasscodeForDevice(recent_, addr));
        return;
    }
    if (size_t(row) >= recent_.size()) return;
    const ui::RecentDevice device = recent_[size_t(row)];
    if (clientSessionKeyCtrl_)
        clientSessionKeyCtrl_->ChangeValue(ToWx(device.sessionKey));
    ConnectWithPrompt(device.addr, device.passcode);
}

void MainFrame::ConnectWithPrompt(const std::string& addr, std::string passcode) {
    std::string target = addr;
    if (!ShowPasscodePrompt(this, target, passcode)) return;
    const uint16_t port = ui::AddressPort(target);
    addrCtrl_->ChangeValue(ToWx(ui::AddressHost(target)));
    connectPortCtrl_->ChangeValue(
        ToWx(std::to_string(port != 0 ? port : deskhub::kDeskhubPort)));
    clientPasscodeCtrl_->ChangeValue(ToWx(ui::TrimAscii(passcode)));
    if (clientSessionKeyCtrl_ && clientSessionKeyCtrl_->GetValue().IsEmpty())
        clientSessionKeyCtrl_->ChangeValue(ToWx(ui::SessionKeyForDevice(recent_, target)));
    StartConnect(target);
}

void MainFrame::OnSourcesReady(const std::string& addr, const std::string& passcode,
    const std::string& sessionKey, const deskhubp::ConnectOutcome& outcome) {
    connectBtn_->Enable();

    if (!outcome.ok) {
        const std::string msg = ui::SourceQueryFailed(addr);
        LOGW("[Connect] %s", msg.c_str());
        SetClientStatus(ToWx(msg), kOffline);
        DeselectAllRows();
        Raise();
        wxMessageBox(ToWx(msg), ToWx(ui::kAppTitle), wxOK | wxICON_WARNING, this);
        return;
    }
    if (outcome.sources.empty()) {
        const std::string msg = ui::SourceQueryEmpty(addr);
        LOGW("[Connect] %s", msg.c_str());
        SetClientStatus(ToWx(msg), kOffline);
        DeselectAllRows();
        Raise();
        wxMessageBox(ToWx(msg), ToWx(ui::kAppTitle), wxOK | wxICON_WARNING, this);
        return;
    }

    SetClientStatus(wxString(), kMutedText);
    const bool encrypted = !sessionKey.empty();
    ui::TouchRecentDevice(recent_, addr, NowUnix(), passcode, encrypted, sessionKey);
    SaveRecentDevices();
    poller_.SetAddresses(AddressesOf(recent_));
    RefreshRecentList();

    std::vector<deskhub::SourceInfo> picked;
    if (!ShowSourcePickerDialog(HWND(GetHandle()), outcome.sources, picked)) {
        DeselectAllRows();
        return;
    }

    OpenViewerSession(addr, passcode, sessionKey, std::move(picked));
    DeselectAllRows();
}

void MainFrame::OpenViewerSession(const std::string& addr, const std::string& passcode,
    const std::string& sessionKey, std::vector<deskhub::SourceInfo> picked) {
    LOGI("[Connect] Opening viewer for %s (%zu source(s)).", addr.c_str(), picked.size());
    std::thread([alive = alive_, addr, passcode, sessionKey, picked = std::move(picked),
                    control = settings_.clientControl] {
        const bool ok = RunViewer(addr, picked, control, passcode, sessionKey);
        if (ok || !wxTheApp) return;
        wxTheApp->CallAfter([alive] {
            if (!*alive) return;
            wxMessageBox(ToWx(ui::kViewerGpuFailed), ToWx(ui::kAppTitle), wxOK | wxICON_WARNING);
        });
    }).detach();
}

bool MainFrame::IsValidSessionKeyHex(const std::string& hex) {
    uint8_t key[deskhub::crypto::kKeySize];
    const bool ok = deskhub::crypto::KeyFromHex(hex, key);
    deskhub::crypto::SecureWipe(std::span<uint8_t>(key, sizeof(key)));
    return ok;
}

std::string MainFrame::ResolvedSessionKey(const std::string& addr) const {
    const std::string typed = ui::TrimAscii(std::string(clientSessionKeyCtrl_->GetValue().utf8_str()));
    if (!typed.empty()) return typed;
    return ui::SessionKeyForDevice(recent_, addr);
}

void MainFrame::RefreshSessionKeyDisplay() {
    if (!sessionKeyCtrl_) return;
    sessionKeyCtrl_->ChangeValue(ToWx(settings_.sessionKeyHex));
}

void MainFrame::SyncSessionCryptoControls() {
    const bool on = encryptSessionCtrl_ && encryptSessionCtrl_->GetValue();
    if (on) deskhubp::EnsureSessionKeyMaterial(settings_, false);
    RefreshSessionKeyDisplay();
    const auto show = [on](wxWindow* w) {
        if (w) w->Show(on);
    };
    show(escrowSessionKeyCtrl_);
    show(escrowHint_);
    show(lifetimeLabel_);
    show(sessionKeyLifetimeCtrl_);
    show(sessionKeyLabel_);
    show(sessionKeyCtrl_);
    show(sessionKeyHint_);
    show(sessionKeyRow_);
    if (escrowSessionKeyCtrl_ && !on) escrowSessionKeyCtrl_->SetValue(false);
    if (settingsPage_) settingsPage_->Layout();
}

void MainFrame::DeselectAllRows() {
    for (long row = 0; row < list_->GetItemCount(); ++row)
        list_->SetItemState(row, 0, wxLIST_STATE_SELECTED);
    for (long row = 0; row < scanList_->GetItemCount(); ++row)
        scanList_->SetItemState(row, 0, wxLIST_STATE_SELECTED);
}

void MainFrame::SaveSettings() {
    settings_.fps = uint32_t(fpsCtrl_->GetValue());
    settings_.bitrateMbps = uint32_t(bitrateCtrl_->GetValue());
    settings_.port = uint32_t(portCtrl_->GetValue());
    settings_.allowInput = allowInputCtrl_->GetValue();
    settings_.clientControl = controlCtrl_->GetValue();
    settings_.runInBackground = runInBackgroundCtrl_->GetValue();
    settings_.hideTrayIcon =
        settings_.runInBackground && hideTrayIconCtrl_->GetValue();
    settings_.clipboardSync = clipboardCtrl_->GetValue();
    settings_.encryptSession = encryptSessionCtrl_->GetValue();
    settings_.escrowSessionKey =
        settings_.encryptSession && escrowSessionKeyCtrl_->GetValue();
    settings_.sessionKeyLifetime = sessionKeyLifetimeCtrl_->GetSelection() == 1
                                       ? ui::SessionKeyLifetime::Persistent
                                       : ui::SessionKeyLifetime::PerShare;
    if (!settings_.encryptSession) settings_.escrowSessionKey = false;
    settings_.logMaxFileMb = uint32_t(logMaxFileMbCtrl_->GetValue());
    settings_.logCompressAfterDays = uint32_t(logCompressDaysCtrl_->GetValue());
    settings_.logDeleteAfterDays = uint32_t(logDeleteDaysCtrl_->GetValue());
    const std::string logDir = ui::TrimAscii(std::string(logDirCtrl_->GetValue().utf8_str()));
    if (logDir.empty()) {
        settings_.logDir.clear();
    } else if (deskhubp::IsUsableLogDir(logDir)) {
        settings_.logDir = logDir;
    } else {
        wxMessageBox(ToWx(ui::kLogDirInvalid), ToWx(ui::kAppTitle), wxOK | wxICON_ERROR, this);
        logDirCtrl_->ChangeValue(ToWx(settings_.logDir));
    }
    const std::string passcode(passcodeCtrl_->GetValue().utf8_str());
    if (deskhub::IsValidPasscode(passcode)) settings_.passcode = passcode;
    const int quality = qualityChoice_->GetSelection();
    if (quality != wxNOT_FOUND)
        settings_.maxDim = deskhub::media::QualityPresetMaxDim(size_t(quality),
            settings_.maxDim);
    const int bindSel = bindChoice_->GetSelection();
    if (bindSel != wxNOT_FOUND && size_t(bindSel) < bindChoices_.size())
        settings_.bindIp = bindChoices_[size_t(bindSel)];
    settings_.autoShare = autoShareCtrl_->GetValue();
    const int languageSel = languageChoice_ ? languageChoice_->GetSelection() : wxNOT_FOUND;
    if (languageSel != wxNOT_FOUND && size_t(languageSel) < ui::kLanguageOptionCount) {
        const ui::UiLanguage chosen = ui::LanguageOptions()[size_t(languageSel)].language;
        settings_.language =
            chosen == ui::UiLanguage::System ? std::string{} : ui::LanguageCode(chosen);
        ui::ApplyUiLanguagePreference(settings_.language, deskhubp::SystemLanguageTag());
    }
    const bool autostart = autostartCtrl_->GetValue();
    if (autostart != settings_.autostart) {
        deskhubp::SetAutostartEnabled(autostart);
        settings_.autostart = deskhubp::AutostartEnabled();
        autostartCtrl_->SetValue(settings_.autostart);
    }
    deskhubp::SaveUiSettings(settings_);
    if (!hosting_ && !hostStarting_) ShowIdleHostState();
}

void MainFrame::RefreshLogView() {
    if (!logFileChoice_ || !logViewCtrl_) return;

    std::string keep;
    const int prev = logFileChoice_->GetSelection();
    if (prev != wxNOT_FOUND && size_t(prev) < logPaths_.size()) keep = logPaths_[size_t(prev)];

    const std::vector<deskhubp::LogFileInfo> files = deskhubp::ListLogFiles();
    logFileChoice_->Clear();
    logPaths_.clear();
    if (files.empty()) {
        logViewCtrl_->ChangeValue(ToWx(ui::kLogEmpty));
        return;
    }

    int sel = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        logFileChoice_->Append(ToWx(files[i].name));
        logPaths_.push_back(files[i].path);
        if (!keep.empty() && files[i].path == keep) sel = int(i);
    }
    logFileChoice_->SetSelection(sel);
    const std::string body = deskhubp::ReadLogFile(logPaths_[size_t(sel)], 512u * 1024u);
    logViewCtrl_->ChangeValue(ToWx(body.empty() ? ui::kLogEmpty.get() : body.c_str()));
}

void MainFrame::SaveRecentDevices() {
    deskhubp::WriteAppDataFile(kRecentDevicesFile, ui::SerializeRecentDevices(recent_));
}

void MainFrame::EnsureTray() {
    if (settings_.hideTrayIcon || tray_) return;
    auto* icon = new SystemMonitorTrayIcon(this);
    icon->SetIcon(wxICON(deskhub_app_icon), ToWx(ui::kAppTitle));
    tray_ = icon;
}

void MainFrame::MinimizeToTray() {
    RemoveTray();
    if (!settings_.hideTrayIcon) EnsureTray();
    Hide();
    if (tray_) {
        tray_->ShowBalloon(ToWx(ui::kAppTitle), ToWx(ui::kBackgroundRunningHint), 4000,
            wxICON_INFORMATION);
    }
}

void MainFrame::RestoreFromTray() {
    CallAfter([this] {
        Show(true);
        Raise();
        Iconize(false);
        ApplyBackgroundSetting();
    });
}

void MainFrame::RemoveTray() {
    if (!tray_) return;
    wxTaskBarIcon* tray = tray_;
    tray_ = nullptr;
    tray->RemoveIcon();
    tray->Destroy();
}

void MainFrame::ApplyBackgroundSetting() {
    if (settings_.runInBackground && !settings_.hideTrayIcon) {
        EnsureTray();
        return;
    }
    RemoveTray();
    if (!settings_.runInBackground && !IsShown()) {
        Show(true);
        Raise();
        Iconize(false);
    }
}

HostShareState MainFrame::CurrentHostShareState() const {
    if (hosting_) return HostShareState::kSharing;
    if (hostStarting_) return HostShareState::kStarting;
    return HostShareState::kIdle;
}

void MainFrame::ApplyTheme() {
    SetBackgroundColour(kAppBg);
    if (book_) book_->SetBackgroundColour(kPageBg);

    if (sidebar_) sidebar_->SetBackgroundColour(kSidebarBg);
    if (sidebarTitle_) sidebarTitle_->SetForegroundColour(kHeadingText);
    if (repoLink_) {
        repoLink_->SetBackgroundColour(kSidebarBg);
        repoLink_->SetNormalColour(kNavText);
        repoLink_->SetVisitedColour(kNavText);
        repoLink_->SetHoverColour(kAccent);
    }
    if (versionLabel_) versionLabel_->SetForegroundColour(kSidebarFootnote);

    TintTagged(hostPage_);
    TintTagged(clientPage_);
    TintTagged(settingsPage_);
    if (hostTableHolder_) TintTagged(hostTableHolder_);
    if (hostTable_) hostTable_->SetBackgroundColour(kSurfaceBg);
    if (hosting_) RebuildHostTable();

    if (clientStatus_) clientStatus_->SetForegroundColour(kMutedText);
    if (hostStatusLabel_) hostStatusLabel_->SetForegroundColour(kMutedText);
    if (connectBtn_) PaintButton(connectBtn_, kAccent);
    if (scanList_) {
        scanList_->SetBackgroundColour(kSurfaceBg);
        scanList_->SetForegroundColour(kHeadingText);
    }
    if (list_) {
        list_->SetBackgroundColour(kSurfaceBg);
        list_->SetForegroundColour(kHeadingText);
    }
    if (hostPicker_) {
        hostPicker_->SetBackgroundColour(kSurfaceBg);
        hostPicker_->SetForegroundColour(kHeadingText);
    }

    ApplyHostState(CurrentHostShareState(),
        hostStatusLabel_ ? hostStatusLabel_->GetLabel() : ToWx(HostPortDetail()));

    for (NavItem* item : pageButtons_) {
        if (item) item->Refresh();
    }
    for (size_t i = 0; i < recent_.size(); ++i) ApplyStatusToRow(long(i), recent_[i].addr);
    for (size_t i = 0; i < scanned_.size(); ++i) ApplyScanPingToRow(long(i), scanned_[i].addr);

    Refresh();
    if (sidebar_) sidebar_->Refresh();
    if (book_) book_->Refresh();
}

void MainFrame::OnSysColourChanged(wxSysColourChangedEvent& event) {
    ApplyTheme();
    event.Skip();
}

void MainFrame::SyncHideTrayControl() {
    if (!hideTrayIconCtrl_ || !runInBackgroundCtrl_) return;
    const bool allow = runInBackgroundCtrl_->GetValue();
    hideTrayIconCtrl_->Show(allow);
    hideTrayIconCtrl_->Enable(allow);
    if (!allow) {
        hideTrayIconCtrl_->SetValue(false);
        settings_.hideTrayIcon = false;
    }
    if (auto* parent = hideTrayIconCtrl_->GetParent()) parent->Layout();
}

void MainFrame::RequestExit() {
    CallAfter([this] {
        if (!ConfirmQuitIfBusy()) return;
        quitting_ = true;
        if (!IsShown()) Show(true);
        Close(true);
    });
}

void MainFrame::Teardown() {
    RemoveTray();
    *alive_ = false;
    hostTimer_.Stop();
    clipTimer_.Stop();
    scanTimer_.Stop();
    scanner_.Cancel();
    agentLoop_.Stop();
    agentDriver_.Join();
    poller_.Stop();
}

bool MainFrame::HasActiveSession() const {
    return hosting_ || hostStarting_ || dh_viewers_open();
}

bool MainFrame::ConfirmQuitIfBusy() {
    if (!HasActiveSession()) return true;
    return ShowQuitBusyPrompt(this);
}

void MainFrame::OnClose(wxCloseEvent& event) {
    if (!quitting_ && event.CanVeto()) {
        if (!settings_.runInBackgroundChoiceMade) {
            bool runInBackground = true;
            const BackgroundPromptResult result =
                ShowBackgroundPrompt(this, runInBackground);
            if (result == BackgroundPromptResult::kConfirm) {
                settings_.runInBackground = runInBackground;
                settings_.runInBackgroundChoiceMade = true;
                runInBackgroundCtrl_->SetValue(runInBackground);
                if (!runInBackground) settings_.hideTrayIcon = false;
                SyncHideTrayControl();
                deskhubp::SaveUiSettings(settings_);
                ApplyBackgroundSetting();
                if (runInBackground) {
                    MinimizeToTray();
                    event.Veto();
                    return;
                }
            }
        } else if (settings_.runInBackground) {
            MinimizeToTray();
            event.Veto();
            return;
        }

        if (!ConfirmQuitIfBusy()) {
            event.Veto();
            return;
        }
    }

    Teardown();
    event.Skip();
}

class SystemMonitorApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppearance(Appearance::System);
        SetAppName(brand::kWindowsServiceName);
        instanceChecker_ = std::make_unique<wxSingleInstanceChecker>(
            std::string(brand::kWindowsServiceName) + "-" + wxGetUserId().ToStdString());
        if (instanceChecker_->IsAnotherRunning()) {
            if (!ActivateExistingInstance()) {
                wxMessageBox(ToWx(ui::kAlreadyRunning), ToWx(ui::kAppTitle), wxOK | wxICON_INFORMATION);
            }
            return false;
        }
        (new MainFrame())->Show(true);
        return true;
    }

private:
    static bool ActivateExistingInstance() {
        const HWND hwnd = FindWindowW(nullptr, ToWx(ui::kAppTitle).wc_str());
        if (!hwnd) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid) AllowSetForegroundWindow(pid);
        return PostMessageW(hwnd, SystemMonitorActivateMsg(), 0, 0) != 0;
    }

    std::unique_ptr<wxSingleInstanceChecker> instanceChecker_;
};

}

wxIMPLEMENT_APP_NO_MAIN(SystemMonitorApp);

int RunSystemMonitorApp() {
    return wxEntry(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOWNORMAL);
}
