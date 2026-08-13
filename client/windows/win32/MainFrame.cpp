#include <wx/wx.h>

#include <wx/dcbuffer.h>
#include <wx/hyperlink.h>
#include <wx/init.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/clipbrd.h>
#include <wx/simplebook.h>
#include <wx/spinctrl.h>
#include <wx/taskbar.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "MainFrame.h"

#include "PasscodePrompt.h"
#include "SourcePickerDialog.h"
#include "Viewer.h"
#include "deskhub/media/QualityPreset.h"
#include "deskhub/media/SourceLabel.h"
#include "deskhub/session/ShareFlow.h"
#include "deskhub/ui/HostRows.h"
#include "deskhub/ui/RecentDevices.h"
#include "deskhub/ui/Strings.h"
#include "deskhub/ui/UiSettings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/media/DisplayEnum.h"
#include "deskhubp/net/DeviceStatusPoller.h"
#include "deskhubp/net/LanScanner.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/AgentDriver.h"
#include "deskhubp/session/AgentLoop.h"
#include "deskhubp/session/ConnectDriver.h"
#include "deskhubp/system/AppDataFile.h"
#include "deskhubp/system/Autostart.h"
#include "deskhubp/system/DeviceName.h"
#include "deskhubp/system/UiSettingsStore.h"

namespace {

namespace ui = deskhub::ui;

constexpr const char* kRecentDevicesFile = "recent-devices.txt";

constexpr int kHostTimerId = 1;
constexpr int kScanTimerId = 2;
constexpr int kClipTimerId = 3;
constexpr int kRescanDelayMs = int(deskhubp::kLanRescanSecs) * 1000;
constexpr int kHintWrapDip = 620;
constexpr int kPrimaryButtonH = 46;

enum Page { kPageHost = 0,
    kPageClient = 1,
    kPageSettings = 2,
    kPageCount = 3 };

const char* const kPageLabels[kPageCount] = {ui::kSidebarHost, ui::kSidebarClient,
    ui::kSidebarSettings};

const wxColour kSidebarBg(31, 41, 55);
const wxColour kSidebarHover(55, 65, 81);
const wxColour kAccent(37, 99, 235);
const wxColour kNavText(209, 213, 219);
const wxColour kSidebarFootnote(148, 163, 184);
const wxColour kHeadingText(17, 24, 39);
const wxColour kMutedText(107, 114, 128);
const wxColour kOnline(0, 145, 60);
const wxColour kOffline(200, 40, 40);
const wxColour kWarning(202, 108, 8);
const wxColour kRowLine(229, 231, 235);
const wxColour kViewerRowBg(249, 250, 251);
const wxColour kBannerIdleBg(243, 244, 246);
const wxColour kBannerLiveBg(232, 250, 239);
const wxColour kBannerBusyBg(235, 243, 255);

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
    heading->SetFont(heading->GetFont().Bold().Scaled(1.35f));
    heading->SetForegroundColour(kHeadingText);
    return heading;
}

wxStaticText* MakeHint(wxWindow* parent, const wxString& text) {
    auto* hint = new wxStaticText(parent, wxID_ANY, text);
    hint->SetForegroundColour(kMutedText);
    hint->Wrap(parent->FromDIP(kHintWrapDip));
    return hint;
}

void SetHintLabel(wxStaticText* hint, const wxString& text) {
    hint->SetLabel(text);
    hint->Wrap(hint->FromDIP(kHintWrapDip));
    hint->GetParent()->Layout();
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
    section->SetFont(section->GetFont().Bold().Scaled(1.1f));
    section->SetForegroundColour(kHeadingText);
    return section;
}

void CopyTextToClipboard(HWND owner, const wxString& text) {
    const std::wstring wide = text.ToStdWstring();
    if (wide.empty() || !OpenClipboard(owner)) return;
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
        if (selected_ || hover_) {
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(selected_ ? kAccent : kSidebarHover));
            dc.DrawRoundedRectangle(rect, FromDIP(8));
        }

        dc.SetFont(selected_ ? GetFont().Bold() : GetFont());
        dc.SetTextForeground(selected_ ? *wxWHITE : kNavText);
        const wxSize extent = dc.GetTextExtent(label_);
        dc.DrawText(label_, FromDIP(16), (rect.GetHeight() - extent.GetHeight()) / 2);
    }

    wxString label_;
    std::function<void()> onClick_;
    bool selected_ = false;
    bool hover_ = false;
};

class MainFrame;

class DeskhubTrayIcon final : public wxTaskBarIcon {
public:
    explicit DeskhubTrayIcon(MainFrame& frame) : frame_(frame) {}

protected:
    wxMenu* CreatePopupMenu() override;

private:
    MainFrame& frame_;
};

class MainFrame final : public wxFrame {
public:
    MainFrame();

private:
    friend class DeskhubTrayIcon;

    void ApplyTrayMode();
    void ToggleWindowFromTray();
    void QuitFromTray();

    wxWindow* BuildSidebar();
    wxWindow* BuildHostPage(wxWindow* parent);
    wxWindow* BuildClientPage(wxWindow* parent);
    wxWindow* BuildSettingsPage(wxWindow* parent);
    static wxTextCtrl* MakePasscodeCtrl(wxWindow* parent);

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
    void ConnectRow(long row, bool scanned);
    void OnSourcesReady(const std::string& addr, const std::string& passcode,
        const deskhubp::ConnectOutcome& outcome);
    void OpenViewerSession(const std::string& addr, const std::string& passcode,
        std::vector<deskhub::SourceInfo> picked);
    void DeselectAllRows();
    void SaveSettings();
    void PopulateBindChoice();
    void RebuildHostAddressRows();
    void SaveRecentDevices();
    void OnClose(wxCloseEvent& event);

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
    wxTextCtrl* deviceNameCtrl_ = nullptr;
    wxSpinCtrl* fpsCtrl_ = nullptr;
    wxSpinCtrl* bitrateCtrl_ = nullptr;
    wxSpinCtrl* portCtrl_ = nullptr;
    wxChoice* qualityChoice_ = nullptr;
    wxCheckBox* allowInputCtrl_ = nullptr;
    wxTextCtrl* passcodeCtrl_ = nullptr;
    wxChoice* bindChoice_ = nullptr;
    wxCheckBox* autoShareCtrl_ = nullptr;
    wxCheckBox* autostartCtrl_ = nullptr;
    wxCheckBox* startHiddenCtrl_ = nullptr;
    wxCheckBox* clipboardCtrl_ = nullptr;
    DeskhubTrayIcon* trayIcon_ = nullptr;
    bool quitting_ = false;
    std::vector<std::string> bindChoices_;

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
    bool hosting_ = false;
    bool hostStarting_ = false;
    bool prompting_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

MainFrame::MainFrame() : wxFrame(nullptr, wxID_ANY, ToWx(ui::kAppTitle)) {
    settings_ = deskhubp::LoadUiSettings();
    recent_ = ui::ParseRecentDevices(deskhubp::ReadAppDataFile(kRecentDevicesFile));

    auto* root = new wxBoxSizer(wxHORIZONTAL);
    root->Add(BuildSidebar(), wxSizerFlags().Expand());

    book_ = new wxSimplebook(this);
    book_->AddPage(BuildHostPage(book_), wxString());
    book_->AddPage(BuildClientPage(book_), wxString());
    book_->AddPage(BuildSettingsPage(book_), wxString());
    root->Add(book_, wxSizerFlags(1).Expand());

    SetIcon(wxICON(deskhub_app_icon));

    SetSizer(root);
    SetMinClientSize(FromDIP(wxSize(1000, 640)));
    SetClientSize(FromDIP(wxSize(1140, 780)));
    Centre();

    hostTimer_.SetOwner(this, kHostTimerId);
    scanTimer_.SetOwner(this, kScanTimerId);
    clipTimer_.SetOwner(this, kClipTimerId);
    Bind(wxEVT_TIMER, &MainFrame::OnHostTimer, this, kHostTimerId);
    Bind(wxEVT_TIMER, &MainFrame::OnScanTimer, this, kScanTimerId);
    Bind(wxEVT_TIMER, &MainFrame::OnClipboardTimer, this, kClipTimerId);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);

    RefreshRecentList();
    StartPoller();
    StartScan();
    SelectPage(kPageClient);
    ApplyTrayMode();

    if (settings_.autoShare) {
        SelectPage(kPageHost);
        CallAfter([this] { OnShare(); });
    }
}

void MainFrame::ApplyTrayMode() {
    if (settings_.startHidden && !trayIcon_) {
        trayIcon_ = new DeskhubTrayIcon(*this);
        if (!trayIcon_->SetIcon(wxICON(deskhub_app_icon), "Deskhub")) {
            delete trayIcon_;
            trayIcon_ = nullptr;
        }
        return;
    }
    if (!settings_.startHidden && trayIcon_) {
        trayIcon_->RemoveIcon();
        delete trayIcon_;
        trayIcon_ = nullptr;
        if (!IsShown()) Show(true);
    }
}

void MainFrame::ToggleWindowFromTray() {
    if (IsShown()) {
        Hide();
        return;
    }
    Show(true);
    Raise();
}

void MainFrame::QuitFromTray() {
    quitting_ = true;
    Close(true);
}

wxWindow* MainFrame::BuildSidebar() {
    auto* panel = new wxPanel(this);
    panel->SetBackgroundColour(kSidebarBg);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(panel, wxID_ANY, "Deskhub");
    title->SetFont(title->GetFont().Bold().Scaled(1.6f));
    title->SetForegroundColour(*wxWHITE);
    sizer->Add(title, wxSizerFlags().Border(wxALL, FromDIP(16)));

    for (int i = 0; i < kPageCount; ++i) {
        auto* item = new NavItem(panel, ToWx(kPageLabels[i]), [this, i] { SelectPage(i); });
        sizer->Add(item, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10)));
        pageButtons_[i] = item;
    }

    sizer->AddStretchSpacer(1);

    auto* repoLink = new wxHyperlinkCtrl(panel, wxID_ANY, ToWx(ui::kProjectLinkLabel),
        ToWx(ui::kProjectUrl));
    repoLink->SetBackgroundColour(kSidebarBg);
    repoLink->SetNormalColour(kNavText);
    repoLink->SetVisitedColour(kNavText);
    repoLink->SetHoverColour(*wxWHITE);
    repoLink->SetToolTip(ToWx(ui::kProjectUrl));
    sizer->Add(repoLink, wxSizerFlags().Border(wxLEFT | wxRIGHT, FromDIP(16)));

    auto* version = new wxStaticText(panel, wxID_ANY, ToWx(ui::VersionLine()));
    version->SetForegroundColour(kSidebarFootnote);
    sizer->Add(version,
        wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM | wxTOP, FromDIP(16)));

    panel->SetSizer(sizer);
    return panel;
}

wxWindow* MainFrame::BuildHostPage(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    panel->SetBackgroundColour(*wxWHITE);
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
    hostPicker_->InsertColumn(0, "Source", wxLIST_FORMAT_LEFT, FromDIP(560));
    sizer->Add(hostPicker_,
        wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(14)));

    hostTableHolder_ = BuildHostTable(panel);
    sizer->Add(hostTableHolder_,
        wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(14)));

    hostHint_ = MakeHint(panel, ToWx(ui::kPickDisplaysHint));
    sizer->Add(hostHint_, pad);

    shareBtn_ = new wxButton(panel, wxID_ANY, wxString());
    shareBtn_->SetMinSize(FromDIP(wxSize(-1, kPrimaryButtonH)));
    shareBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnShare(); });
    sizer->Add(shareBtn_, wxSizerFlags().Expand().Border(wxALL, FromDIP(14)));

    panel->SetSizer(sizer);
    ShowIdleHostState();
    RefreshDisplayChoices();
    return panel;
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
            ToWx(settings_.bindIp + "  (" + ui::kBindNotConnectedNote + ")"));
        bindChoices_.push_back(settings_.bindIp);
        active = int(bindChoices_.size() - 1);
    }
    bindChoice_->SetSelection(active);
}

void MainFrame::RebuildHostAddressRows() {
    hostAddrPanel_->DestroyChildren();
    auto* holder = new wxBoxSizer(wxVERTICAL);
    std::vector<AdapterAddr> shown;
    for (const auto& a : ListLocalIPv4())
        if (settings_.bindIp.empty() || a.ip == settings_.bindIp) shown.push_back(a);
    if (shown.empty()) {
        const std::string text = settings_.bindIp.empty()
                                     ? std::string(ui::kNoNetworkAddress)
                                     : settings_.bindIp + "  (" + ui::kBindNotConnectedNote + ")";
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
            auto* copy = new wxButton(hostAddrPanel_, wxID_ANY, "Copy");
            copy->SetMinSize(FromDIP(wxSize(84, 32)));
            const wxString ip = ToWx(a.ip);
            copy->Bind(wxEVT_BUTTON, [this, ip](wxCommandEvent&) {
                CopyTextToClipboard(HWND(GetHandle()), ip);
            });
            grid->Add(copy);
        }
        holder->Add(grid, wxSizerFlags(1).Expand());
    }
    hostAddrPanel_->SetSizer(holder);
    hostAddrPanel_->GetParent()->Layout();
}

wxTextCtrl* MainFrame::MakePasscodeCtrl(wxWindow* parent) {
    auto* ctrl = new wxTextCtrl(parent, wxID_ANY, wxString(), wxDefaultPosition,
        parent->FromDIP(wxSize(64, -1)), wxTE_PROCESS_ENTER,
        wxTextValidator(wxFILTER_DIGITS));
    ctrl->SetMaxLength(deskhub::kPasscodeDigits);
    return ctrl;
}

wxWindow* MainFrame::BuildClientPage(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    panel->SetBackgroundColour(*wxWHITE);
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
        FromDIP(wxSize(260, -1)), wxTE_PROCESS_ENTER);
    addrCtrl_->SetName("address-field");
    addrCtrl_->SetHint(ToWx(ui::kClientIpPlaceholder));
    addrCtrl_->Bind(wxEVT_TEXT_ENTER, connectNow);
    grid->Add(addrCtrl_, wxSizerFlags().CentreVertical());

    grid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kUdpPortLabel)),
        wxSizerFlags().CentreVertical());
    connectPortCtrl_ = new wxTextCtrl(panel, wxID_ANY,
        ToWx(std::to_string(deskhub::kDeskhubPort)), wxDefaultPosition,
        FromDIP(wxSize(80, -1)), wxTE_PROCESS_ENTER);
    connectPortCtrl_->Bind(wxEVT_TEXT_ENTER, connectNow);
    grid->Add(connectPortCtrl_, wxSizerFlags().CentreVertical());

    grid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kClientPasscodePrompt)),
        wxSizerFlags().CentreVertical());
    clientPasscodeCtrl_ = MakePasscodeCtrl(panel);
    clientPasscodeCtrl_->SetName("passcode-field");
    clientPasscodeCtrl_->SetToolTip(ToWx(ui::kClientPasscodeHint));
    clientPasscodeCtrl_->Bind(wxEVT_TEXT_ENTER, connectNow);
    grid->Add(clientPasscodeCtrl_, wxSizerFlags().CentreVertical());

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
    scanList_->InsertColumn(0, "Device", wxLIST_FORMAT_LEFT, FromDIP(170));
    scanList_->InsertColumn(1, "Ping", wxLIST_FORMAT_RIGHT, FromDIP(70));
    scanList_->SetMinSize(FromDIP(wxSize(-1, 130)));
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
    list_->InsertColumn(0, "Device", wxLIST_FORMAT_LEFT, FromDIP(170));
    list_->InsertColumn(1, "Status", wxLIST_FORMAT_LEFT, FromDIP(100));
    list_->InsertColumn(2, "Ping", wxLIST_FORMAT_RIGHT, FromDIP(70));
    list_->InsertColumn(3, "Last connected", wxLIST_FORMAT_LEFT, FromDIP(150));
    list_->SetMinSize(FromDIP(wxSize(-1, 130)));
    list_->Bind(wxEVT_LEFT_DOWN,
        [this](wxMouseEvent& event) { OnListClick(list_, event, false); });
    sizer->Add(list_, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    listHint_ = MakeHint(panel, ToWx(ui::kRecentDevicesEmpty));
    sizer->Add(listHint_, wxSizerFlags().Border(wxALL, FromDIP(16)));

    panel->SetSizer(sizer);
    return panel;
}

wxWindow* MainFrame::BuildSettingsPage(wxWindow* parent) {
    auto* panel = new wxPanel(parent);
    panel->SetBackgroundColour(*wxWHITE);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    const wxSizerFlags pad = wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    sizer->Add(MakeHeading(panel, ui::kSettingsHeading), pad);
    sizer->Add(MakeHint(panel, ToWx(ui::kSettingsHint)), pad);

    sizer->AddSpacer(FromDIP(8));
    sizer->Add(MakeSection(panel, "Video"), pad);
    auto* videoGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));

    videoGrid->Add(new wxStaticText(panel, wxID_ANY, "FPS"), wxSizerFlags().CentreVertical());
    fpsCtrl_ = new wxSpinCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, 1, int(ui::kMaxSettingsFps), int(settings_.fps));
    videoGrid->Add(fpsCtrl_);

    videoGrid->Add(new wxStaticText(panel, wxID_ANY, "Bitrate (Mbps)"),
        wxSizerFlags().CentreVertical());
    bitrateCtrl_ = new wxSpinCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, 1, int(ui::kMaxSettingsBitrateMbps), int(settings_.bitrateMbps));
    videoGrid->Add(bitrateCtrl_);

    videoGrid->Add(new wxStaticText(panel, wxID_ANY, "Quality"),
        wxSizerFlags().CentreVertical());
    qualityChoice_ = new wxChoice(panel, wxID_ANY);
    for (const auto& preset : deskhub::media::kQualityPresets)
        qualityChoice_->Append(ToWx(preset.label));
    qualityChoice_->SetSelection(int(deskhub::media::QualityPresetIndex(settings_.maxDim)));
    videoGrid->Add(qualityChoice_);

    sizer->Add(videoGrid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    sizer->AddSpacer(FromDIP(12));
    sizer->Add(MakeSection(panel, "Connection"), pad);
    auto* netGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));

    netGrid->Add(new wxStaticText(panel, wxID_ANY, "UDP port"),
        wxSizerFlags().CentreVertical());
    portCtrl_ = new wxSpinCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, 1, int(ui::kMaxSettingsPort), int(settings_.port));
    netGrid->Add(portCtrl_);

    sizer->Add(netGrid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    sizer->AddSpacer(FromDIP(12));
    sizer->Add(MakeSection(panel, "Security"), pad);
    auto* securityGrid = new wxFlexGridSizer(2, FromDIP(wxSize(14, 10)));
    securityGrid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kPasscodeLabel)),
        wxSizerFlags().CentreVertical());
    passcodeCtrl_ = MakePasscodeCtrl(panel);
    passcodeCtrl_->SetValue(ToWx(settings_.passcode));
    securityGrid->Add(passcodeCtrl_);
    sizer->Add(securityGrid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    sizer->AddSpacer(FromDIP(12));
    sizer->Add(MakeSection(panel, "Permissions"), pad);
    allowInputCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kAllowControlLabel));
    allowInputCtrl_->SetValue(settings_.allowInput);
    sizer->Add(allowInputCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    clipboardCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kClipboardSyncLabel));
    clipboardCtrl_->SetValue(settings_.clipboardSync);
    sizer->Add(clipboardCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    sizer->AddSpacer(FromDIP(12));
    sizer->Add(MakeSection(panel, "Startup"), pad);
    autostartCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kAutostartLabel));
    autostartCtrl_->SetValue(deskhubp::AutostartEnabled());
    sizer->Add(autostartCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    autoShareCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kAutoShareLabel));
    autoShareCtrl_->SetValue(settings_.autoShare);
    sizer->Add(autoShareCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));
    startHiddenCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kCloseToTrayLabel));
    startHiddenCtrl_->SetValue(settings_.startHidden);
    sizer->Add(startHiddenCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    fpsCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    fpsCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    bitrateCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    bitrateCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    portCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    portCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    qualityChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SaveSettings(); });
    allowInputCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
    clipboardCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
    autoShareCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
    autostartCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
    startHiddenCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
    passcodeCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });

    panel->SetSizer(sizer);
    return panel;
}

void MainFrame::SelectPage(int page) {
    for (int i = 0; i < kPageCount; ++i) pageButtons_[i]->SetSelected(i == page);
    book_->ChangeSelection(size_t(page));
    if (page == kPageHost && !hosting_ && !hostStarting_) RefreshDisplayChoices();
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
    card->SetBackgroundColour(kRowLine);
    auto* cardSizer = new wxBoxSizer(wxVERTICAL);

    auto* holder = new wxPanel(card);
    holder->SetBackgroundColour(*wxWHITE);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* header = new wxPanel(holder);
    header->SetBackgroundColour(kBannerIdleBg);
    header->SetMinSize(FromDIP(wxSize(-1, 30)));
    auto* headerRow = new wxBoxSizer(wxHORIZONTAL);
    headerRow->AddSpacer(FromDIP(kHostRowBarWidth + kHostCellGap));
    for (const HostColumn& column : kHostColumns) {
        auto* title = new wxStaticText(header, wxID_ANY, ToWx(column.title).Upper(),
            wxDefaultPosition, FromDIP(wxSize(column.width, -1)), column.align);
        title->SetForegroundColour(kMutedText);
        title->SetFont(title->GetFont().Bold().Scaled(0.85f));
        headerRow->Add(title, wxSizerFlags().CentreVertical().Border(wxRIGHT,
                                  FromDIP(kHostCellGap)));
    }
    headerRow->AddSpacer(FromDIP(kHostActionWidth));
    header->SetSizer(headerRow);
    sizer->Add(header, wxSizerFlags().Expand());

    auto* headerLine = new wxWindow(holder, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 1)));
    headerLine->SetBackgroundColour(kRowLine);
    sizer->Add(headerLine, wxSizerFlags().Expand());

    hostTable_ = new wxScrolledWindow(holder);
    hostTable_->SetBackgroundColour(*wxWHITE);
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
        view.panel->SetBackgroundColour(ref.viewer ? kViewerRowBg : *wxWHITE);
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
        StopHosting();
        return;
    }

    if (availableDisplays_.empty()) {
        const std::string err = deskhubp::ListDisplaysError();
        wxMessageBox(err.empty() ? wxString("No display found to share.") : ToWx(err),
            "Deskhub", wxOK | wxICON_WARNING, this);
        return;
    }

    std::vector<AgentSource> chosen;
    for (size_t i = 0; i < availableDisplays_.size(); ++i) {
        if (long(i) >= hostPicker_->GetItemCount()) break;
        if (hostPicker_->IsItemChecked(long(i))) chosen.push_back(availableDisplays_[i]);
    }
    if (chosen.empty()) {
        wxMessageBox(ToWx(ui::kNoDisplayTicked), "Deskhub", wxOK | wxICON_WARNING, this);
        return;
    }

    const deskhub::ShareClampResult clamp = deskhub::ClampShareSources(chosen);
    if (clamp.clamped)
        wxMessageBox(ToWx(ui::ShareClampWarning()), "Deskhub", wxOK | wxICON_WARNING, this);

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
        wxMessageBox(ToWx(std::string(ui::kShareStartFailed) + ".\n\n" + error), "Deskhub",
            wxOK | wxICON_ERROR, this);
        return;
    }

    hosting_ = true;
    std::string status = ui::SharingStatusLine(port);
    if (!passcode.empty()) status += " " + ui::PasscodeNote(passcode);
    if (!allowInput) status += std::string(" ") + ui::kViewOnlyNote;
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

void MainFrame::StopHosting() {
    hostTimer_.Stop();
    clipTimer_.Stop();
    agentLoop_.Stop();
    agentDriver_.Join();
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
    if (addr.empty()) {
        wxMessageBox("Enter the host machine's IP address first (e.g., 192.168.1.10).",
            "Deskhub", wxOK | wxICON_WARNING, this);
        return;
    }

    NetAddr server{};
    if (!ParseNetAddr(addr, server)) {
        wxMessageBox(ToWx("Invalid address: \"" + addr + "\"\n" + ui::InvalidAddressHint()),
            "Deskhub", wxOK | wxICON_ERROR, this);
        return;
    }

    const std::string typed(clientPasscodeCtrl_->GetValue().utf8_str());
    const std::string passcode = deskhub::IsValidPasscode(typed)
                                     ? typed
                                     : ui::PasscodeForDevice(recent_, addr);
    if (!deskhub::IsValidPasscode(passcode)) {
        wxMessageBox(ToWx(ui::kPasscodeInvalid), "Deskhub", wxOK | wxICON_ERROR, this);
        clientPasscodeCtrl_->SetFocus();
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
        [this, addr, passcode](const deskhubp::ConnectOutcome& outcome) {
            OnSourcesReady(addr, passcode, outcome);
        });
    if (!started) {
        SetClientStatus(ToWx(ui::kQueryingSources), kMutedText);
        return;
    }
    connectBtn_->Disable();
    SetClientStatus(ToWx(ui::kQueryingSources), kMutedText);
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
    StartConnect(target);
}

void MainFrame::OnSourcesReady(const std::string& addr, const std::string& passcode,
    const deskhubp::ConnectOutcome& outcome) {
    connectBtn_->Enable();

    if (!outcome.ok) {
        SetClientStatus(ToWx(ui::SourceQueryFailed(addr)), kOffline);
        DeselectAllRows();
        return;
    }
    if (outcome.sources.empty()) {
        SetClientStatus(ToWx(ui::SourceQueryEmpty(addr)), kOffline);
        DeselectAllRows();
        return;
    }

    SetClientStatus(wxString(), kMutedText);
    ui::TouchRecentDevice(recent_, addr, NowUnix(), passcode);
    SaveRecentDevices();
    poller_.SetAddresses(AddressesOf(recent_));
    RefreshRecentList();

    std::vector<deskhub::SourceInfo> picked;
    if (!ShowSourcePickerDialog(HWND(GetHandle()), outcome.sources, picked)) {
        DeselectAllRows();
        return;
    }

    OpenViewerSession(addr, passcode, std::move(picked));
    DeselectAllRows();
}

void MainFrame::OpenViewerSession(const std::string& addr, const std::string& passcode,
    std::vector<deskhub::SourceInfo> picked) {
    std::thread([addr, passcode, picked = std::move(picked),
                    control = settings_.clientControl] {
        RunViewer(addr, picked, control, passcode);
    }).detach();
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
    settings_.clipboardSync = clipboardCtrl_->GetValue();
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
    settings_.startHidden = startHiddenCtrl_->GetValue();
    ApplyTrayMode();
    const bool autostart = autostartCtrl_->GetValue();
    if (autostart != settings_.autostart) {
        deskhubp::SetAutostartEnabled(autostart);
        settings_.autostart = deskhubp::AutostartEnabled();
        autostartCtrl_->SetValue(settings_.autostart);
    }
    deskhubp::SaveUiSettings(settings_);
    if (!hosting_ && !hostStarting_) ShowIdleHostState();
}

void MainFrame::SaveRecentDevices() {
    deskhubp::WriteAppDataFile(kRecentDevicesFile, ui::SerializeRecentDevices(recent_));
}

void MainFrame::OnClose(wxCloseEvent& event) {
    if (!quitting_ && settings_.startHidden && trayIcon_ && event.CanVeto()) {
        event.Veto();
        Hide();
        return;
    }
    if (trayIcon_) {
        trayIcon_->RemoveIcon();
        delete trayIcon_;
        trayIcon_ = nullptr;
    }
    *alive_ = false;
    hostTimer_.Stop();
    clipTimer_.Stop();
    scanTimer_.Stop();
    scanner_.Cancel();
    agentLoop_.Stop();
    agentDriver_.Join();
    poller_.Stop();
    event.Skip();
}

wxMenu* DeskhubTrayIcon::CreatePopupMenu() {
    auto* menu = new wxMenu();
    const int toggleWindowId =
        menu->Append(wxID_ANY,
                ToWx(frame_.IsShown() ? ui::kTrayHideWindow : ui::kTrayShowWindow))
            ->GetId();
    const int toggleShareId =
        menu->Append(wxID_ANY, ToWx(frame_.hosting_ ? ui::kStopSharing : ui::kStartSharing))
            ->GetId();
    menu->AppendSeparator();
    const int quitId = menu->Append(wxID_ANY, ToWx(ui::kTrayQuit))->GetId();

    menu->Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { frame_.ToggleWindowFromTray(); }, toggleWindowId);
    menu->Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { frame_.OnShare(); }, toggleShareId);
    menu->Bind(
        wxEVT_MENU, [this](wxCommandEvent&) { frame_.QuitFromTray(); }, quitId);
    return menu;
}

class DeskhubApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("Deskhub");
        auto* frame = new MainFrame();
        frame->Show();
        return true;
    }

    int FilterEvent(wxEvent& event) override {
        if (event.GetEventType() == wxEVT_LEFT_DOWN || event.GetEventType() == wxEVT_LEFT_UP) {
            const auto* win = dynamic_cast<wxWindow*>(event.GetEventObject());
            const wxString name = win ? win->GetName() : wxString("?");
            const wxString label = win ? win->GetLabel().Left(24) : wxString();
            LOGI("[UI] Mouse %s on \"%s\" (%s).",
                event.GetEventType() == wxEVT_LEFT_DOWN ? "down" : "up",
                std::string(name.utf8_str()).c_str(), std::string(label.utf8_str()).c_str());
        }
        return -1;
    }
};

}

wxIMPLEMENT_APP_NO_MAIN(DeskhubApp);

int RunDeskhubApp() {
    return wxEntry(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOWNORMAL);
}
