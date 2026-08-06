#include <wx/wx.h>

#include <wx/dcbuffer.h>
#include <wx/hyperlink.h>
#include <wx/init.h>
#include <wx/listctrl.h>
#include <wx/simplebook.h>
#include <wx/spinctrl.h>

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
#include "deskhub/session/ShareFlow.h"
#include "deskhub/ui/RecentDevices.h"
#include "deskhub/ui/Strings.h"
#include "deskhub/ui/UiSettings.h"
#include "deskhubp/media/DisplayEnum.h"
#include "deskhubp/net/DeviceStatusPoller.h"
#include "deskhubp/net/LanScanner.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/AgentDriver.h"
#include "deskhubp/session/AgentLoop.h"
#include "deskhubp/session/ConnectDriver.h"
#include "deskhubp/system/AppDataFile.h"
#include "deskhubp/system/UiSettingsStore.h"

namespace {

namespace ui = deskhub::ui;

constexpr const char* kRecentDevicesFile = "recent-devices.txt";

constexpr int kHostTimerId = 1;
constexpr int kScanTimerId = 2;
constexpr int kRescanDelayMs = 45'000;

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
    return hint;
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

class MainFrame final : public wxFrame {
public:
    MainFrame();

private:
    wxWindow* BuildSidebar();
    wxWindow* BuildHostPage(wxWindow* parent);
    wxWindow* BuildClientPage(wxWindow* parent);
    wxWindow* BuildSettingsPage(wxWindow* parent);
    static wxTextCtrl* MakePasscodeCtrl(wxWindow* parent);

    void SelectPage(int page);
    void RefreshRecentList();
    void ApplyStatusToRow(long row, const std::string& addr);
    void StartPoller();
    void OnDeviceStatus(const deskhubp::DeviceStatus& status);

    void OnShare();
    void StartHosting(const std::vector<AgentSource>& sources, const AgentOptions& options);
    void OnHostStarted(bool started, const std::string& error, uint16_t port,
        bool allowInput, const std::string& passcode);
    void StopHosting();
    void OnHostTimer(wxTimerEvent& event);
    void RefreshDisplayChoices();
    void UpdateHostRows(const std::vector<AgentSourceStatus>& rows);
    void SetHostCell(long row, int col, const wxString& text);
    long SelectedHostRow() const;
    void RelayoutHostPage();
    void UpdateHostButtons();
    void OnStopSelectedDisplay();
    void OnKickSelectedViewer();
    std::string IdleHostStatus() const;

    void StartConnect(const std::string& addr);
    void ConnectWithPrompt(const std::string& addr, std::string passcode);
    void StartScan();
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
    void SaveRecentDevices();
    void OnClose(wxCloseEvent& event);

    wxSimplebook* book_ = nullptr;
    NavItem* pageButtons_[kPageCount] = {};
    wxTextCtrl* addrCtrl_ = nullptr;
    wxButton* connectBtn_ = nullptr;
    wxListCtrl* scanList_ = nullptr;
    wxStaticText* scanStatus_ = nullptr;
    wxCheckBox* controlCtrl_ = nullptr;
    wxListCtrl* list_ = nullptr;
    wxStaticText* listHint_ = nullptr;
    wxStaticText* hostStatusLabel_ = nullptr;
    wxStaticText* hostHint_ = nullptr;
    wxListCtrl* hostList_ = nullptr;
    wxButton* stopDisplayBtn_ = nullptr;
    wxButton* kickViewerBtn_ = nullptr;
    wxButton* shareBtn_ = nullptr;
    wxTextCtrl* clientPasscodeCtrl_ = nullptr;
    wxSpinCtrl* fpsCtrl_ = nullptr;
    wxSpinCtrl* bitrateCtrl_ = nullptr;
    wxSpinCtrl* portCtrl_ = nullptr;
    wxChoice* qualityChoice_ = nullptr;
    wxCheckBox* allowInputCtrl_ = nullptr;
    wxTextCtrl* passcodeCtrl_ = nullptr;

    struct HostRow {
        bool viewer = false;
        uint8_t sourceId = 0;
        std::string viewerAddr;

        bool operator==(const HostRow&) const = default;
    };

    deskhub::ui::UiSettings settings_;
    std::vector<AgentSource> availableDisplays_;
    std::vector<HostRow> hostRows_;
    std::vector<ui::RecentDevice> recent_;
    std::vector<deskhubp::ScanHit> scanned_;
    std::vector<std::string> scannedThisRound_;
    std::map<std::string, deskhubp::DeviceStatus> statusByAddr_;
    deskhubp::ConnectDriver connectDriver_;
    deskhubp::DeviceStatusPoller poller_;
    deskhubp::LanScanner scanner_;
    AgentLoop agentLoop_;
    deskhubp::AgentDriver agentDriver_;
    wxTimer hostTimer_;
    wxTimer scanTimer_;
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
    SetMinClientSize(FromDIP(wxSize(720, 620)));
    SetClientSize(FromDIP(wxSize(860, 700)));
    Centre();

    hostTimer_.SetOwner(this, kHostTimerId);
    scanTimer_.SetOwner(this, kScanTimerId);
    Bind(wxEVT_TIMER, &MainFrame::OnHostTimer, this, kHostTimerId);
    Bind(wxEVT_TIMER, &MainFrame::OnScanTimer, this, kScanTimerId);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);

    RefreshRecentList();
    StartPoller();
    StartScan();
    SelectPage(kPageClient);
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

    const auto addrs = ListLocalIPv4();
    if (addrs.empty()) {
        sizer->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kNoNetworkAddress)), pad);
    } else {
        auto* grid = new wxFlexGridSizer(3, FromDIP(wxSize(14, 10)));
        grid->AddGrowableCol(1, 1);
        for (const auto& a : addrs) {
            grid->Add(new wxStaticText(panel, wxID_ANY, ToWx(a.name)),
                wxSizerFlags().CentreVertical());
            auto* ipText = new wxStaticText(panel, wxID_ANY, ToWx(a.ip));
            ipText->SetFont(ipText->GetFont().Bold());
            grid->Add(ipText, wxSizerFlags().CentreVertical());
            auto* copy = new wxButton(panel, wxID_ANY, "Copy");
            copy->SetMinSize(FromDIP(wxSize(84, 32)));
            const wxString ip = ToWx(a.ip);
            copy->Bind(wxEVT_BUTTON, [this, ip](wxCommandEvent&) {
                CopyTextToClipboard(HWND(GetHandle()), ip);
            });
            grid->Add(copy);
        }
        sizer->Add(grid, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(14)));
    }

    hostStatusLabel_ = new wxStaticText(panel, wxID_ANY, ToWx(IdleHostStatus()));
    hostStatusLabel_->SetFont(hostStatusLabel_->GetFont().Bold());
    hostStatusLabel_->SetForegroundColour(kMutedText);
    sizer->Add(hostStatusLabel_, pad);

    hostList_ = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL);
    hostList_->InsertColumn(0, "Source", wxLIST_FORMAT_LEFT, FromDIP(140));
    hostList_->InsertColumn(1, "Size", wxLIST_FORMAT_LEFT, FromDIP(80));
    hostList_->InsertColumn(2, "Viewers", wxLIST_FORMAT_RIGHT, FromDIP(58));
    hostList_->InsertColumn(3, "Client", wxLIST_FORMAT_LEFT, FromDIP(120));
    hostList_->InsertColumn(4, "Capture", wxLIST_FORMAT_RIGHT, FromDIP(58));
    hostList_->InsertColumn(5, "Send", wxLIST_FORMAT_RIGHT, FromDIP(50));
    hostList_->InsertColumn(6, "Mbps", wxLIST_FORMAT_RIGHT, FromDIP(55));
    hostList_->InsertColumn(7, "RTT", wxLIST_FORMAT_RIGHT, FromDIP(55));
    hostList_->Bind(wxEVT_LIST_ITEM_SELECTED,
        [this](wxListEvent&) { UpdateHostButtons(); });
    hostList_->Bind(wxEVT_LIST_ITEM_DESELECTED,
        [this](wxListEvent&) { UpdateHostButtons(); });
    sizer->Add(hostList_, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(14)));

    hostHint_ = MakeHint(panel, ToWx(ui::kPickDisplaysHint));
    sizer->Add(hostHint_, pad);

    auto* manageRow = new wxBoxSizer(wxHORIZONTAL);
    stopDisplayBtn_ = new wxButton(panel, wxID_ANY, ToWx(ui::kStopSelectedDisplay));
    stopDisplayBtn_->SetMinSize(FromDIP(wxSize(170, 32)));
    stopDisplayBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnStopSelectedDisplay(); });
    manageRow->Add(stopDisplayBtn_);
    kickViewerBtn_ = new wxButton(panel, wxID_ANY, ToWx(ui::kDisconnectSelectedViewer));
    kickViewerBtn_->SetMinSize(FromDIP(wxSize(210, 32)));
    kickViewerBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnKickSelectedViewer(); });
    manageRow->Add(kickViewerBtn_, wxSizerFlags().Border(wxLEFT, FromDIP(10)));
    sizer->Add(manageRow, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(14)));
    stopDisplayBtn_->Hide();
    kickViewerBtn_->Hide();

    shareBtn_ = new wxButton(panel, wxID_ANY, ToWx(ui::kShareButton));
    shareBtn_->SetMinSize(FromDIP(wxSize(-1, 40)));
    shareBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnShare(); });
    sizer->Add(shareBtn_, wxSizerFlags().Expand().Border(wxALL, FromDIP(14)));

    panel->SetSizer(sizer);
    RefreshDisplayChoices();
    return panel;
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
        StartConnect(std::string(addrCtrl_->GetValue().utf8_str()));
    };

    auto* grid = new wxFlexGridSizer(2, FromDIP(wxSize(12, 12)));

    grid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kClientIpPrompt)),
        wxSizerFlags().CentreVertical());
    addrCtrl_ = new wxTextCtrl(panel, wxID_ANY, wxString(), wxDefaultPosition,
        FromDIP(wxSize(260, -1)), wxTE_PROCESS_ENTER);
    addrCtrl_->SetHint(ToWx(ui::kClientIpPlaceholder));
    addrCtrl_->Bind(wxEVT_TEXT_ENTER, connectNow);
    grid->Add(addrCtrl_, wxSizerFlags().CentreVertical());

    grid->Add(new wxStaticText(panel, wxID_ANY, ToWx(ui::kClientPasscodePrompt)),
        wxSizerFlags().CentreVertical());
    clientPasscodeCtrl_ = MakePasscodeCtrl(panel);
    clientPasscodeCtrl_->SetToolTip(ToWx(ui::kClientPasscodeHint));
    clientPasscodeCtrl_->Bind(wxEVT_TEXT_ENTER, connectNow);
    grid->Add(clientPasscodeCtrl_, wxSizerFlags().CentreVertical());

    sizer->Add(grid, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(16)));

    connectBtn_ = new wxButton(panel, wxID_ANY, "Connect");
    connectBtn_->SetMinSize(FromDIP(wxSize(140, 32)));
    connectBtn_->Bind(wxEVT_BUTTON, connectNow);
    sizer->Add(connectBtn_, wxSizerFlags().Centre().Border(wxTOP, FromDIP(14)));

    controlCtrl_ = new wxCheckBox(panel, wxID_ANY, ToWx(ui::kRequestControlLabel));
    controlCtrl_->SetValue(settings_.clientControl);
    controlCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        settings_.clientControl = controlCtrl_->GetValue();
        deskhubp::SaveUiSettings(settings_);
    });
    sizer->Add(controlCtrl_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(20)));

    sizer->Add(MakeHeading(panel, ui::kLanDevicesHeading), pad);

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

    sizer->Add(MakeHeading(panel, ui::kRecentDevicesHeading), pad);

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

    fpsCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    fpsCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    bitrateCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    bitrateCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    portCtrl_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { SaveSettings(); });
    portCtrl_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { SaveSettings(); });
    qualityChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SaveSettings(); });
    allowInputCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { SaveSettings(); });
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
    hostList_->DeleteAllItems();
    hostList_->EnableCheckBoxes(true);
    for (size_t i = 0; i < availableDisplays_.size(); ++i) {
        const long row = hostList_->InsertItem(long(i), ToWx(availableDisplays_[i].name));
        hostList_->CheckItem(row, true);
    }
    UpdateHostButtons();
}

void MainFrame::RefreshRecentList() {
    list_->DeleteAllItems();
    for (size_t i = 0; i < recent_.size(); ++i) {
        const auto& device = recent_[i];
        const long row = list_->InsertItem(long(i), ToWx(device.addr));
        list_->SetItem(row, 3, ToWx(FormatLastConnected(device.lastConnectedUnix)));
        ApplyStatusToRow(row, device.addr);
    }
    listHint_->SetLabel(
        ToWx(recent_.empty() ? ui::kRecentDevicesEmpty : ui::kRecentDevicesHint));
}

void MainFrame::ApplyStatusToRow(long row, const std::string& addr) {
    const auto it = statusByAddr_.find(addr);
    if (it == statusByAddr_.end()) {
        list_->SetItem(row, 1, ToWx(ui::kStatusChecking));
        list_->SetItem(row, 2, "-");
        list_->SetItemTextColour(row, wxColour(120, 120, 120));
        return;
    }
    const bool online = it->second.online;
    list_->SetItem(row, 1, ToWx(online ? ui::kStatusOnline : ui::kStatusOffline));
    list_->SetItem(row, 2, online ? ToWx(ui::PingMs(it->second.rttMs)) : wxString("-"));
    list_->SetItemTextColour(row, online ? kOnline : kOffline);
}

void MainFrame::StartPoller() {
    poller_.SetAddresses(AddressesOf(recent_));
    poller_.Start([this](const deskhubp::DeviceStatus& status) {
        CallAfter([this, status] { OnDeviceStatus(status); });
    });
}

void MainFrame::OnDeviceStatus(const deskhubp::DeviceStatus& status) {
    statusByAddr_[status.addr] = status;
    for (size_t i = 0; i < recent_.size(); ++i)
        if (recent_[i].addr == status.addr) ApplyStatusToRow(long(i), status.addr);
}

std::string MainFrame::IdleHostStatus() const {
    return std::string(ui::kNotSharing) + " " + ui::UdpPortLine(uint16_t(settings_.port)) + ".";
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
        if (long(i) >= hostList_->GetItemCount()) break;
        if (hostList_->IsItemChecked(long(i))) chosen.push_back(availableDisplays_[i]);
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

    StartHosting(sources, options);
}

void MainFrame::StartHosting(const std::vector<AgentSource>& sources,
    const AgentOptions& options) {
    hostStarting_ = true;
    shareBtn_->Disable();
    hostStatusLabel_->SetLabel(ToWx(ui::kStartingShare));
    hostList_->EnableCheckBoxes(false);
    hostList_->DeleteAllItems();
    hostRows_.clear();
    hostHint_->Hide();
    RelayoutHostPage();

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
        hostStatusLabel_->SetLabel(ToWx(IdleHostStatus()));
        hostStatusLabel_->SetForegroundColour(kMutedText);
        hostHint_->Show();
        RefreshDisplayChoices();
        RelayoutHostPage();
        wxMessageBox(ToWx(std::string(ui::kShareStartFailed) + ".\n\n" + error), "Deskhub",
            wxOK | wxICON_ERROR, this);
        return;
    }

    hosting_ = true;
    shareBtn_->SetLabel(ToWx(ui::kStopSharing));
    std::string status = ui::SharingStatusLine(port);
    if (!passcode.empty()) status += " " + ui::PasscodeNote(passcode);
    if (!allowInput) status += std::string(" ") + ui::kViewOnlyNote;
    hostStatusLabel_->SetLabel(ToWx(status));
    hostStatusLabel_->SetForegroundColour(kOnline);
    stopDisplayBtn_->Show();
    kickViewerBtn_->Show();
    UpdateHostButtons();
    hostTimer_.Start(int(deskhubp::kAgentStatusPollMs));
    RelayoutHostPage();
}

void MainFrame::StopHosting() {
    hostTimer_.Stop();
    agentLoop_.Stop();
    agentDriver_.Join();
    hosting_ = false;
    shareBtn_->SetLabel(ToWx(ui::kShareButton));
    hostStatusLabel_->SetLabel(ToWx(IdleHostStatus()));
    hostStatusLabel_->SetForegroundColour(kMutedText);
    stopDisplayBtn_->Hide();
    kickViewerBtn_->Hide();
    hostHint_->Show();
    RefreshDisplayChoices();
    RelayoutHostPage();
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
    std::vector<HostRow> refs;
    for (const AgentSourceStatus& s : rows) {
        refs.push_back(HostRow{false, s.sourceId, {}});
        for (const std::string& addr : s.viewerAddrs)
            refs.push_back(HostRow{true, s.sourceId, addr});
    }

    if (refs != hostRows_) {
        hostRows_ = std::move(refs);
        hostList_->DeleteAllItems();
        for (size_t i = 0; i < hostRows_.size(); ++i)
            hostList_->InsertItem(long(i), wxString());
        UpdateHostButtons();
    }

    for (size_t i = 0; i < hostRows_.size(); ++i) {
        const HostRow& ref = hostRows_[i];
        const long row = long(i);
        const AgentSourceStatus* s = nullptr;
        for (const AgentSourceStatus& candidate : rows)
            if (candidate.sourceId == ref.sourceId) s = &candidate;
        if (!s) continue;

        if (ref.viewer) {
            SetHostCell(row, 0, wxString::FromUTF8("    \xE2\x86\xB3 viewer"));
            SetHostCell(row, 1, wxString());
            SetHostCell(row, 2, wxString());
            SetHostCell(row, 3, ToWx(ref.viewerAddr));
            SetHostCell(row, 4, wxString());
            SetHostCell(row, 5, wxString());
            SetHostCell(row, 6, wxString());
            SetHostCell(row, 7, wxString());
            hostList_->SetItemTextColour(row, kOnline);
            continue;
        }

        SetHostCell(row, 0, ToWx(s->name));
        SetHostCell(row, 1, wxString::Format("%ux%u", s->width, s->height));
        SetHostCell(row, 2, wxString::Format("%u", s->viewerCount));
        SetHostCell(row, 3, wxString());
        SetHostCell(row, 4, wxString::Format("%.0f", s->captureFps));
        SetHostCell(row, 5, wxString::Format("%.0f", s->sendFps));
        SetHostCell(row, 6, wxString::Format("%.1f", s->sendKbps / 1000.0));
        SetHostCell(row, 7, s->viewerConnected ? ToWx(ui::PingMs(s->rttMs)) : wxString("-"));
        hostList_->SetItemTextColour(row,
            s->viewerConnected ? kOnline
                               : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    }
}

void MainFrame::SetHostCell(long row, int col, const wxString& text) {
    if (hostList_->GetItemText(row, col) != text) hostList_->SetItem(row, col, text);
}

long MainFrame::SelectedHostRow() const {
    return hostList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

void MainFrame::RelayoutHostPage() {
    hostList_->GetParent()->Layout();
}

void MainFrame::UpdateHostButtons() {
    if (!stopDisplayBtn_ || !kickViewerBtn_) return;
    const long row = SelectedHostRow();
    const bool valid = hosting_ && row >= 0 && size_t(row) < hostRows_.size();
    stopDisplayBtn_->Enable(valid && !hostRows_[size_t(row)].viewer);
    kickViewerBtn_->Enable(valid && hostRows_[size_t(row)].viewer);
}

void MainFrame::OnStopSelectedDisplay() {
    const long row = SelectedHostRow();
    if (!hosting_ || row < 0 || size_t(row) >= hostRows_.size()) return;
    const HostRow& ref = hostRows_[size_t(row)];
    if (ref.viewer) return;
    agentLoop_.StopSource(ref.sourceId);
}

void MainFrame::OnKickSelectedViewer() {
    const long row = SelectedHostRow();
    if (!hosting_ || row < 0 || size_t(row) >= hostRows_.size()) return;
    const HostRow& ref = hostRows_[size_t(row)];
    if (!ref.viewer) return;
    NetAddr addr{};
    if (!ParseNetAddr(ref.viewerAddr, addr)) return;
    agentLoop_.KickViewer(ref.sourceId, addr.Pack());
}

void MainFrame::StartConnect(const std::string& rawAddr) {
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
    if (started) connectBtn_->Disable();
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

void MainFrame::OnScanTimer(wxTimerEvent&) {
    StartScan();
}

void MainFrame::OnScanHit(const deskhubp::ScanHit& hit) {
    scannedThisRound_.push_back(hit.addr);
    for (deskhubp::ScanHit& known : scanned_) {
        if (known.addr != hit.addr) continue;
        known.rttMs = hit.rttMs;
        RefreshScanList();
        return;
    }
    scanned_.push_back(hit);
    RefreshScanList();
}

void MainFrame::OnScanProgress(const deskhubp::ScanProgress& progress) {
    scanStatus_->SetLabel(
        ToWx(ui::ScanningStatus(progress.probed, progress.total, uint16_t(settings_.port))));
}

void MainFrame::OnScanFinished(const deskhubp::ScanProgress& progress) {
    const auto gone = [this](const deskhubp::ScanHit& hit) {
        return std::find(scannedThisRound_.begin(), scannedThisRound_.end(), hit.addr) ==
               scannedThisRound_.end();
    };
    scanned_.erase(std::remove_if(scanned_.begin(), scanned_.end(), gone), scanned_.end());
    RefreshScanList();

    const char* const note = scanned_.empty() ? ui::kScanRescanNote : ui::kLanDevicesHint;
    scanStatus_->SetLabel(
        progress.total == 0
            ? ToWx(ui::kScanNoLocalNetwork)
            : ToWx(ui::ScanFinishedStatus(scanned_.size(), progress.total) + " " + note));
    scanTimer_.StartOnce(kRescanDelayMs);
}

void MainFrame::RefreshScanList() {
    scanList_->DeleteAllItems();
    for (size_t i = 0; i < scanned_.size(); ++i) {
        const long row = scanList_->InsertItem(long(i), ToWx(scanned_[i].addr));
        scanList_->SetItem(row, 1, ToWx(ui::PingMs(scanned_[i].rttMs)));
        scanList_->SetItemTextColour(row, kOnline);
    }
}

void MainFrame::OnListClick(wxListCtrl* list, wxMouseEvent& event, bool scanned) {
    event.Skip();
    int flags = 0;
    const long row = list->HitTest(event.GetPosition(), flags);

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
    if (!ShowPasscodePrompt(this, addr, passcode)) return;
    addrCtrl_->ChangeValue(ToWx(addr));
    clientPasscodeCtrl_->ChangeValue(ToWx(ui::TrimAscii(passcode)));
    StartConnect(addr);
}

void MainFrame::OnSourcesReady(const std::string& addr, const std::string& passcode,
    const deskhubp::ConnectOutcome& outcome) {
    connectBtn_->Enable();

    if (outcome.ok) {
        ui::TouchRecentDevice(recent_, addr, NowUnix(), passcode);
        SaveRecentDevices();
        poller_.SetAddresses(AddressesOf(recent_));
        RefreshRecentList();
    }

    std::vector<deskhub::SourceInfo> picked;
    if (outcome.hasSources()) {
        if (!ShowSourcePickerDialog(HWND(GetHandle()), outcome.sources, picked)) {
            DeselectAllRows();
            return;
        }
    } else {
        picked = deskhubp::DefaultViewTargets();
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
    const std::string passcode(passcodeCtrl_->GetValue().utf8_str());
    if (deskhub::IsValidPasscode(passcode)) settings_.passcode = passcode;
    const int quality = qualityChoice_->GetSelection();
    if (quality != wxNOT_FOUND)
        settings_.maxDim = deskhub::media::QualityPresetMaxDim(size_t(quality),
            settings_.maxDim);
    deskhubp::SaveUiSettings(settings_);
    if (!hosting_ && !hostStarting_) hostStatusLabel_->SetLabel(ToWx(IdleHostStatus()));
}

void MainFrame::SaveRecentDevices() {
    deskhubp::WriteAppDataFile(kRecentDevicesFile, ui::SerializeRecentDevices(recent_));
}

void MainFrame::OnClose(wxCloseEvent& event) {
    *alive_ = false;
    hostTimer_.Stop();
    scanTimer_.Stop();
    scanner_.Cancel();
    agentLoop_.Stop();
    agentDriver_.Join();
    poller_.Stop();
    event.Skip();
}

class DeskhubApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("Deskhub");
        (new MainFrame())->Show(true);
        return true;
    }
};

}

wxIMPLEMENT_APP_NO_MAIN(DeskhubApp);

int RunDeskhubApp() {
    return wxEntry(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOWNORMAL);
}
