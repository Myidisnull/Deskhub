#include "TerminalPage.h"

#include <wx/dcbuffer.h>
#include <wx/listctrl.h>
#include <wx/statline.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "deskhub/net/BindAddress.h"
#include "deskhub/terminal/KeyEncoder.h"
#include "deskhub/terminal/Palette.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/session/TerminalHost.h"
#include "deskhubp/session/TerminalViewer.h"
#include "deskhubp/system/DeviceName.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/TrustStoreFile.h"

namespace {

namespace ui = deskhub::ui;
namespace term = deskhub::term;

constexpr int kRedrawTimerId = 41;
constexpr int kSessionTimerId = 42;
constexpr int kRedrawMs = 33;
constexpr int kSessionMs = 750;
constexpr int kHintWrap = 620;
constexpr int kGridPadding = 6;

const wxColour kHeadingText(17, 24, 39);
const wxColour kMutedText(107, 114, 128);
const wxColour kWarnText(180, 60, 0);
const wxColour kWarnBg(255, 246, 230);

wxString ToWx(const std::string& s) {
    return wxString::FromUTF8(s.c_str(), s.size());
}

wxString ToWx(const char* s) {
    return wxString::FromUTF8(s);
}

wxColour ToWxColour(const term::Rgb& rgb) {
    return wxColour(rgb.r, rgb.g, rgb.b);
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
    hint->Wrap(parent->FromDIP(kHintWrap));
    return hint;
}

bool IsPrintableKey(int code) {
    return code >= 32 && code != WXK_DELETE;
}

term::TermMods ModsOf(const wxKeyEvent& event) {
    term::TermMods mods;
    mods.shift = event.ShiftDown();
    mods.alt = event.AltDown();
    mods.ctrl = event.ControlDown();
    return mods;
}

bool NamedKeyOf(int code, term::TermKey& out) {
    switch (code) {
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER: out = term::TermKey::Enter; return true;
    case WXK_BACK: out = term::TermKey::Backspace; return true;
    case WXK_TAB: out = term::TermKey::Tab; return true;
    case WXK_ESCAPE: out = term::TermKey::Escape; return true;
    case WXK_UP: out = term::TermKey::Up; return true;
    case WXK_DOWN: out = term::TermKey::Down; return true;
    case WXK_LEFT: out = term::TermKey::Left; return true;
    case WXK_RIGHT: out = term::TermKey::Right; return true;
    case WXK_HOME: out = term::TermKey::Home; return true;
    case WXK_END: out = term::TermKey::End; return true;
    case WXK_PAGEUP: out = term::TermKey::PageUp; return true;
    case WXK_PAGEDOWN: out = term::TermKey::PageDown; return true;
    case WXK_INSERT: out = term::TermKey::Insert; return true;
    case WXK_DELETE: out = term::TermKey::Delete; return true;
    case WXK_F1: out = term::TermKey::F1; return true;
    case WXK_F2: out = term::TermKey::F2; return true;
    case WXK_F3: out = term::TermKey::F3; return true;
    case WXK_F4: out = term::TermKey::F4; return true;
    case WXK_F5: out = term::TermKey::F5; return true;
    case WXK_F6: out = term::TermKey::F6; return true;
    case WXK_F7: out = term::TermKey::F7; return true;
    case WXK_F8: out = term::TermKey::F8; return true;
    case WXK_F9: out = term::TermKey::F9; return true;
    case WXK_F10: out = term::TermKey::F10; return true;
    case WXK_F11: out = term::TermKey::F11; return true;
    case WXK_F12: out = term::TermKey::F12; return true;
    default: return false;
    }
}

// Paints the grid core/terminal/ hands over. It knows nothing about escape
// sequences: every client draws the same cells and sends the same key bytes.
class TerminalGrid final : public wxWindow {
public:
    // wxWANTS_CHARS is what stops the frame's dialog navigation from eating
    // Enter, Tab and the arrow keys before the terminal ever sees them.
    explicit TerminalGrid(wxWindow* parent)
        : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS) {
        snapshot_.size = deskhub::TermSize{0, 0};
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(ToWxColour(term::kDefaultBackground));
        SetFont(wxFont(wxFontInfo(11).FaceName("Consolas").Family(wxFONTFAMILY_TELETYPE)));
        MeasureCell();
        SetMinSize(FromDIP(wxSize(640, 320)));
        Bind(wxEVT_PAINT, &TerminalGrid::OnPaint, this);
        Bind(wxEVT_SIZE, &TerminalGrid::OnSize, this);
        Bind(wxEVT_KEY_DOWN, &TerminalGrid::OnKeyDown, this);
        Bind(wxEVT_CHAR, &TerminalGrid::OnChar, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { SetFocus(); });
        Bind(wxEVT_MOUSEWHEEL, &TerminalGrid::OnWheel, this);
        Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent&) { Refresh(); });
        Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent&) { Refresh(); });
    }

    bool AcceptsFocus() const override {
        return true;
    }
    bool AcceptsFocusFromKeyboard() const override {
        return true;
    }

    void Attach(deskhubp::TerminalViewer* viewer) {
        viewer_ = viewer;
        PullSnapshot();
    }

    void PullSnapshot() {
        if (viewer_ != nullptr && viewer_->Running()) {
            const size_t was = snapshot_.scrollbackRows;
            snapshot_ = viewer_->Snapshot(scrollOffset_);
            if (scrollOffset_ > 0 && snapshot_.scrollbackRows > was) {
                scrollOffset_ += snapshot_.scrollbackRows - was;
                snapshot_ = viewer_->Snapshot(scrollOffset_);
            }
            scrollOffset_ = snapshot_.scrollOffset;
        } else {
            snapshot_ = deskhubp::TerminalSnapshot{};
            snapshot_.size = deskhub::TermSize{0, 0};
            scrollOffset_ = 0;
        }
        Refresh(false);
    }

    void ScrollBy(int rows) {
        const size_t before = scrollOffset_;
        if (rows > 0) scrollOffset_ += size_t(rows);
        else scrollOffset_ -= std::min(scrollOffset_, size_t(-rows));
        if (scrollOffset_ > snapshot_.scrollbackRows) scrollOffset_ = snapshot_.scrollbackRows;
        if (scrollOffset_ != before) PullSnapshot();
    }

    void ScrollToBottom() {
        if (scrollOffset_ == 0) return;
        scrollOffset_ = 0;
        PullSnapshot();
    }

    deskhub::TermSize CellsThatFit() const {
        const wxSize client = GetClientSize();
        const int cols = (client.GetWidth() - 2 * kGridPadding) / std::max(1, cellWidth_);
        const int rows = (client.GetHeight() - 2 * kGridPadding) / std::max(1, cellHeight_);
        return deskhub::ClampTermSize(deskhub::TermSize{uint16_t(std::max(1, cols)),
            uint16_t(std::max(1, rows))});
    }

private:
    void MeasureCell() {
        wxClientDC dc(this);
        dc.SetFont(GetFont());
        const wxSize extent = dc.GetTextExtent("M");
        cellWidth_ = std::max(1, extent.GetWidth());
        cellHeight_ = std::max(1, extent.GetHeight());
    }

    void OnSize(wxSizeEvent& event) {
        if (onResize_) onResize_();
        Refresh(false);
        event.Skip();
    }

    void OnWheel(wxMouseEvent& event) {
        const int lines = event.GetLinesPerAction() > 0 ? event.GetLinesPerAction() : 3;
        ScrollBy(event.GetWheelRotation() > 0 ? lines : -lines);
    }

    void OnKeyDown(wxKeyEvent& event) {
        term::TermKey named{};
        const int code = event.GetKeyCode();
        if (event.ShiftDown() && (code == WXK_PAGEUP || code == WXK_PAGEDOWN)) {
            const int page = std::max(1, snapshot_.size.rows - 1);
            ScrollBy(code == WXK_PAGEUP ? page : -page);
            return;
        }
        ScrollToBottom();
        if (viewer_ != nullptr && NamedKeyOf(event.GetKeyCode(), named)) {
            term::TermKeyEvent key;
            key.key = named;
            key.mods = ModsOf(event);
            viewer_->SendKey(key);
            return;
        }
        if (viewer_ != nullptr && event.ControlDown() && !event.AltDown()) {
            const int unicode = event.GetUnicodeKey();
            if (unicode != WXK_NONE && unicode > 0) {
                term::TermKeyEvent key;
                key.key = term::TermKey::Char;
                key.codepoint = char32_t(unicode);
                key.mods = ModsOf(event);
                viewer_->SendKey(key);
                return;
            }
        }
        event.Skip();
    }

    void OnChar(wxKeyEvent& event) {
        ScrollToBottom();
        const int code = event.GetUnicodeKey();
        if (viewer_ == nullptr || code == WXK_NONE || !IsPrintableKey(code)) {
            event.Skip();
            return;
        }
        if (event.ControlDown()) return;
        term::TermKeyEvent key;
        key.key = term::TermKey::Char;
        key.codepoint = char32_t(code);
        key.mods.alt = event.AltDown();
        viewer_->SendKey(key);
    }

    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(ToWxColour(term::kDefaultBackground)));
        dc.Clear();
        dc.SetFont(GetFont());

        for (uint16_t row = 0; row < snapshot_.size.rows; ++row) {
            for (uint16_t col = 0; col < snapshot_.size.cols; ++col) {
                const term::Cell& cell = snapshot_.At(row, col);
                const term::CellColors colors = term::ResolveCell(cell.pen, false);
                const int x = kGridPadding + col * cellWidth_;
                const int y = kGridPadding + row * cellHeight_;

                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(ToWxColour(colors.bg)));
                dc.DrawRectangle(x, y, cellWidth_, cellHeight_);

                if (cell.ch == U' ') continue;
                wxFont font = GetFont();
                if ((cell.pen.attrs & term::kAttrBold) != 0) font = font.Bold();
                if ((cell.pen.attrs & term::kAttrItalic) != 0) font = font.Italic();
                if ((cell.pen.attrs & term::kAttrUnderline) != 0) font = font.Underlined();
                dc.SetFont(font);
                dc.SetTextForeground(ToWxColour(colors.fg));
                dc.DrawText(ToWx(term::EncodeUtf8(cell.ch)), x, y);
            }
        }

        if (snapshot_.cursor.visible && snapshot_.size.rows > 0) {
            const int x = kGridPadding + snapshot_.cursor.col * cellWidth_;
            const int y = kGridPadding + snapshot_.cursor.row * cellHeight_;
            dc.SetPen(wxPen(ToWxColour(term::kCursorColor)));
            dc.SetBrush(HasFocus() ? wxBrush(ToWxColour(term::kCursorColor))
                                   : *wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(x, y, cellWidth_, cellHeight_);
        }
    }

public:
    std::function<void()> onResize_{};

private:
    deskhubp::TerminalViewer* viewer_ = nullptr;
    deskhubp::TerminalSnapshot snapshot_{};
    size_t scrollOffset_ = 0;
    int cellWidth_ = 8;
    int cellHeight_ = 16;
};

class TerminalPanel final : public wxPanel {
public:
    TerminalPanel(wxWindow* parent, deskhub::ui::UiSettings& settings,
        std::function<void()> onSettingsChanged)
        : wxPanel(parent), settings_(settings), onSettingsChanged_(std::move(onSettingsChanged)) {
        SetBackgroundColour(*wxWHITE);
        Build();

        redrawTimer_.SetOwner(this, kRedrawTimerId);
        sessionTimer_.SetOwner(this, kSessionTimerId);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { grid_->PullSnapshot(); }, kRedrawTimerId);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { RefreshSessions(); }, kSessionTimerId);
        sessionTimer_.Start(kSessionMs);
    }

    ~TerminalPanel() override {
        Shutdown();
    }

    void Shutdown() {
        redrawTimer_.Stop();
        sessionTimer_.Stop();
        viewer_.Stop();
        host_.Stop();
    }

    void StartAutoShare() {
        if (settings_.terminalAutoShare) StartSharing();
    }

private:
    void Build() {
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const wxSizerFlags pad = wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

        sizer->Add(MakeHeading(this, ui::kTerminalHostHeading), pad);
        sizer->Add(MakeHint(this, ToWx(ui::kTerminalHostHint)), pad);

        auto* hostRow = new wxBoxSizer(wxHORIZONTAL);
        shareBtn_ = new wxButton(this, wxID_ANY, ToWx(ui::kTerminalShareButton));
        shareBtn_->SetName("terminal-share-button");
        shareBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ToggleSharing(); });
        hostRow->Add(shareBtn_, wxSizerFlags().CentreVertical());

        hostStatus_ = new wxStaticText(this, wxID_ANY, ToWx(ui::kTerminalSharingOff));
        hostStatus_->SetForegroundColour(kMutedText);
        hostRow->Add(hostStatus_, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(14)));
        sizer->Add(hostRow, pad);

        auto* netRow = new wxBoxSizer(wxHORIZONTAL);
        netRow->Add(new wxStaticText(this, wxID_ANY, ToWx(ui::kTerminalNetworkLabel)),
            wxSizerFlags().CentreVertical());
        bindChoice_ = new wxChoice(this, wxID_ANY);
        bindChoice_->SetName("terminal-network-choice");
        PopulateBindChoice();
        bindChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SaveBindChoice(); });
        netRow->Add(bindChoice_, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(12)));

        netRow->Add(new wxStaticText(this, wxID_ANY, ToWx(ui::kTerminalPortLabel)),
            wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(16)));
        portCtrl_ = new wxTextCtrl(this, wxID_ANY, ToWx(std::to_string(settings_.terminalPort)),
            wxDefaultPosition, FromDIP(wxSize(80, -1)));
        portCtrl_->SetName("terminal-port");
        portCtrl_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
            SaveTerminalPort();
            event.Skip();
        });
        netRow->Add(portCtrl_, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(10)));
        sizer->Add(netRow, pad);

        autoShareCtrl_ = new wxCheckBox(this, wxID_ANY, ToWx(ui::kTerminalAutoShareLabel));
        autoShareCtrl_->SetName("terminal-auto-share");
        autoShareCtrl_->SetValue(settings_.terminalAutoShare);
        autoShareCtrl_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { OnAutoShareToggled(); });
        sizer->Add(autoShareCtrl_, pad);

        autoShareWarning_ = new wxStaticText(this, wxID_ANY, ToWx(ui::kTerminalAutoShareWarning));
        autoShareWarning_->SetForegroundColour(kWarnText);
        autoShareWarning_->SetBackgroundColour(kWarnBg);
        autoShareWarning_->Wrap(FromDIP(kHintWrap));
        autoShareWarning_->Show(settings_.terminalAutoShare);
        sizer->Add(autoShareWarning_, pad);

        sessions_ = new wxStaticText(this, wxID_ANY, ToWx(ui::kTerminalNoSessions));
        sessions_->SetName("terminal-sessions");
        sessions_->SetForegroundColour(kMutedText);
        sizer->Add(new wxStaticText(this, wxID_ANY, ToWx(ui::kTerminalOpenSessionsHeading)), pad);
        sizer->Add(sessions_, pad);

        sizer->Add(new wxStaticLine(this), wxSizerFlags().Expand().Border(wxALL, FromDIP(14)));

        sizer->Add(MakeHeading(this, ui::kTerminalClientHeading),
            wxSizerFlags().Border(wxLEFT | wxRIGHT, FromDIP(14)));
        sizer->Add(MakeHint(this, ToWx(ui::kTerminalClientHint)), pad);

        auto* clientRow = new wxBoxSizer(wxHORIZONTAL);
        clientRow->Add(new wxStaticText(this, wxID_ANY, ToWx(ui::kClientIpPrompt)),
            wxSizerFlags().CentreVertical());
        addrCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString(), wxDefaultPosition,
            FromDIP(wxSize(200, -1)), wxTE_PROCESS_ENTER);
        addrCtrl_->SetName("terminal-address");
        addrCtrl_->SetHint(ToWx(ui::kClientIpPlaceholder));
        addrCtrl_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { OpenShell(); });
        clientRow->Add(addrCtrl_, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(10)));

        clientRow->Add(new wxStaticText(this, wxID_ANY, ToWx(ui::kUdpPortLabel)),
            wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(14)));
        clientPortCtrl_ = new wxTextCtrl(this, wxID_ANY,
            ToWx(std::to_string(settings_.terminalPort)), wxDefaultPosition,
            FromDIP(wxSize(80, -1)), wxTE_PROCESS_ENTER);
        clientPortCtrl_->SetName("terminal-connect-port");
        clientPortCtrl_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { OpenShell(); });
        clientRow->Add(clientPortCtrl_,
            wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(10)));

        clientRow->Add(new wxStaticText(this, wxID_ANY, ToWx(ui::kClientPasscodePrompt)),
            wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(14)));
        passcodeCtrl_ = new wxTextCtrl(this, wxID_ANY, wxString(), wxDefaultPosition,
            FromDIP(wxSize(80, -1)), wxTE_PROCESS_ENTER);
        passcodeCtrl_->SetName("terminal-passcode");
        passcodeCtrl_->SetMaxLength(deskhub::kPasscodeDigits);
        passcodeCtrl_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { OpenShell(); });
        clientRow->Add(passcodeCtrl_, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(10)));

        openBtn_ = new wxButton(this, wxID_ANY, ToWx(ui::kTerminalOpenButton));
        openBtn_->SetName("terminal-open-button");
        openBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenShell(); });
        clientRow->Add(openBtn_, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(14)));

        closeBtn_ = new wxButton(this, wxID_ANY, ToWx(ui::kTerminalCloseButton));
        closeBtn_->SetName("terminal-close-button");
        closeBtn_->Enable(false);
        closeBtn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CloseShell(); });
        clientRow->Add(closeBtn_, wxSizerFlags().CentreVertical().Border(wxLEFT, FromDIP(8)));
        sizer->Add(clientRow, pad);

        clientStatus_ = new wxStaticText(this, wxID_ANY, wxString());
        clientStatus_->SetName("terminal-client-status");
        clientStatus_->SetForegroundColour(kMutedText);
        sizer->Add(clientStatus_, pad);

        grid_ = new TerminalGrid(this);
        grid_->onResize_ = [this] { ResizeToGrid(); };
        sizer->Add(grid_, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(14)));

        SetSizer(sizer);
    }

    void PopulateBindChoice() {
        adapters_.clear();
        bindChoice_->Clear();
        bindChoice_->Append(ToWx(ui::kBindAllInterfaces));
        int selected = 0;
        for (const AdapterAddr& adapter : ListLocalIPv4()) {
            adapters_.push_back(adapter.ip);
            bindChoice_->Append(ToWx(adapter.name + "  (" + adapter.ip + ")"));
            if (adapter.ip == settings_.terminalBindIp)
                selected = int(adapters_.size());
        }
        bindChoice_->SetSelection(selected);
    }

    void SaveTerminalPort() {
        const uint16_t port = ui::PortOrDefault(std::string(portCtrl_->GetValue().utf8_str()));
        if (port == settings_.terminalPort) return;
        settings_.terminalPort = port;
        portCtrl_->SetValue(ToWx(std::to_string(port)));
        if (onSettingsChanged_) onSettingsChanged_();
    }

    void SaveBindChoice() {
        const int at = bindChoice_->GetSelection();
        settings_.terminalBindIp =
            at > 0 && size_t(at) <= adapters_.size() ? adapters_[size_t(at) - 1] : std::string();
        if (onSettingsChanged_) onSettingsChanged_();
    }

    void OnAutoShareToggled() {
        settings_.terminalAutoShare = autoShareCtrl_->GetValue();
        autoShareWarning_->Show(settings_.terminalAutoShare);
        Layout();
        if (onSettingsChanged_) onSettingsChanged_();
    }

    void ToggleSharing() {
        if (host_.Running()) StopSharing();
        else StartSharing();
    }

    void StartSharing() {
        if (host_.Running()) return;
        if (!deskhubp::QuicAvailable()) {
            hostStatus_->SetLabel(ToWx(ui::kTerminalHostUnavailable));
            return;
        }
        const std::string name = settings_.deviceName.empty() ? deskhubp::LocalDeviceName()
                                                              : settings_.deviceName;
        const deskhubp::HostIdentity identity = deskhubp::LoadOrCreateHostIdentity(name);
        if (!identity.Valid()) {
            hostStatus_->SetLabel(ToWx(ui::kTerminalHostUnavailable));
            return;
        }

        const deskhub::BindChoice bind =
            deskhub::SelectBindAddress(settings_.terminalBindIp, AdapterIps());
        deskhubp::TerminalHostConfig config;
        config.bindIp = bind.ip;
        config.port = uint16_t(settings_.terminalPort);
        config.passcode = settings_.passcode;

        deskhubp::TerminalHostCallbacks hooks;
        hooks.onSessionsChanged = [this] { CallAfter([this] { RefreshSessions(); }); };

        if (!host_.Start(config, identity, std::move(hooks))) {
            hostStatus_->SetLabel(ToWx(host_.LastPortInUse() ? ui::kTerminalPortInUse
                                                             : ui::kShareStartFailed));
            return;
        }
        settings_.terminalShare = true;
        if (onSettingsChanged_) onSettingsChanged_();
        shareBtn_->SetLabel(ToWx(ui::kTerminalStopSharing));
        hostStatus_->SetLabel(ToWx(std::string(ui::kTerminalSharingOn) + "  port " +
            std::to_string(config.port) + "  " +
            deskhub::FormatFingerprint(identity.fingerprint)));
        RefreshSessions();
    }

    void StopSharing() {
        host_.Stop();
        settings_.terminalShare = false;
        if (onSettingsChanged_) onSettingsChanged_();
        shareBtn_->SetLabel(ToWx(ui::kTerminalShareButton));
        hostStatus_->SetLabel(ToWx(ui::kTerminalSharingOff));
        RefreshSessions();
    }

    std::vector<std::string> AdapterIps() const {
        return adapters_;
    }

    void RefreshSessions() {
        if (!host_.Running()) {
            sessions_->SetLabel(ToWx(ui::kTerminalNoSessions));
            return;
        }
        const std::vector<deskhub::TerminalRecord> rows = host_.Sessions();
        if (rows.empty()) {
            sessions_->SetLabel(ToWx(ui::kTerminalNoSessions));
            return;
        }
        std::string text;
        for (const deskhub::TerminalRecord& row : rows) {
            if (!text.empty()) text += "\n";
            text += row.clientEndpoint;
            if (!row.clientName.empty()) text += "  " + row.clientName;
            if (row.state == deskhub::TerminalState::Detached) text += "  (detached)";
        }
        sessions_->SetLabel(ToWx(text));
        Layout();
    }

    void OpenShell() {
        if (viewer_.Running()) return;
        const std::string typed = std::string(addrCtrl_->GetValue().utf8_str());
        const uint16_t port =
            ui::PortOrDefault(std::string(clientPortCtrl_->GetValue().utf8_str()));
        const std::string address = ui::AddressWithPort(typed, port);
        NetAddr host{};
        if (address.empty() || !ParseNetAddr(address, host)) {
            SetClientStatus(ui::kTerminalUnreachable);
            return;
        }
        const std::string passcode = std::string(passcodeCtrl_->GetValue().utf8_str());
        if (!deskhub::IsValidPasscode(passcode)) {
            SetClientStatus(ui::kPasscodeInvalid);
            return;
        }

        deskhubp::TerminalViewerConfig config;
        config.host = host;
        config.hostLabel = ui::AddressHost(address);
        config.passcode = passcode;
        config.clientName = settings_.deviceName.empty() ? deskhubp::LocalDeviceName()
                                                         : settings_.deviceName;
        config.size = grid_->CellsThatFit();

        deskhubp::TerminalViewerCallbacks hooks;
        hooks.onState = [this](deskhubp::TerminalViewerState state, std::string_view message) {
            const std::string copy(message);
            CallAfter([this, state, copy] { OnViewerState(state, copy); });
        };
        hooks.onTrustAsked = [this](deskhub::TrustVerdict verdict, std::string_view fingerprint) {
            const std::string copy(fingerprint);
            CallAfter([this, verdict, copy] { AskAboutKey(verdict, copy); });
        };

        if (!viewer_.Start(config, std::move(hooks))) {
            SetClientStatus(ui::kTerminalUnreachable);
            return;
        }
        grid_->Attach(&viewer_);
        grid_->SetFocus();
        openBtn_->Enable(false);
        closeBtn_->Enable(true);
        redrawTimer_.Start(kRedrawMs);
        SetClientStatus(ui::kTerminalConnecting);
    }

    void CloseShell() {
        redrawTimer_.Stop();
        viewer_.Stop();
        grid_->Attach(nullptr);
        openBtn_->Enable(true);
        closeBtn_->Enable(false);
        SetClientStatus(ui::kTerminalClosed);
    }

    void ResizeToGrid() {
        if (!viewer_.Running()) return;
        viewer_.Resize(grid_->CellsThatFit());
    }

    void SetClientStatus(const char* text) {
        clientStatus_->SetLabel(ToWx(text));
    }

    void OnViewerState(deskhubp::TerminalViewerState state, const std::string& message) {
        clientStatus_->SetLabel(ToWx(message));
        if (state == deskhubp::TerminalViewerState::Live) grid_->SetFocus();
        if (state == deskhubp::TerminalViewerState::Ended ||
            state == deskhubp::TerminalViewerState::Failed ||
            state == deskhubp::TerminalViewerState::Refused) {
            redrawTimer_.Stop();
            openBtn_->Enable(true);
            closeBtn_->Enable(false);
        }
    }

    void AskAboutKey(deskhub::TrustVerdict verdict, const std::string& fingerprint) {
        const bool changed = verdict == deskhub::TrustVerdict::Changed;
        wxString body = ToWx(changed ? ui::kTrustChangedBody : ui::kTrustNewHostBody);
        body += "\n\n";
        body += ToWx(ui::kTrustFingerprintLabel);
        body += " ";
        body += ToWx(fingerprint);

        wxMessageDialog dialog(this, body,
            ToWx(changed ? ui::kTrustChangedTitle : ui::kTrustNewHostTitle),
            wxYES_NO | wxNO_DEFAULT | (changed ? wxICON_WARNING : wxICON_QUESTION));
        dialog.SetYesNoLabels(ToWx(ui::kTrustAccept), ToWx(ui::kTrustReject));
        if (dialog.ShowModal() == wxID_YES) viewer_.AcceptFingerprint();
        else viewer_.RejectFingerprint();
    }

    deskhub::ui::UiSettings& settings_;
    std::function<void()> onSettingsChanged_;

    deskhubp::TerminalHost host_{};
    deskhubp::TerminalViewer viewer_{};

    wxButton* shareBtn_ = nullptr;
    wxStaticText* hostStatus_ = nullptr;
    wxChoice* bindChoice_ = nullptr;
    wxTextCtrl* portCtrl_ = nullptr;
    wxCheckBox* autoShareCtrl_ = nullptr;
    wxStaticText* autoShareWarning_ = nullptr;
    wxStaticText* sessions_ = nullptr;
    wxTextCtrl* addrCtrl_ = nullptr;
    wxTextCtrl* clientPortCtrl_ = nullptr;
    wxTextCtrl* passcodeCtrl_ = nullptr;
    wxButton* openBtn_ = nullptr;
    wxButton* closeBtn_ = nullptr;
    wxStaticText* clientStatus_ = nullptr;
    TerminalGrid* grid_ = nullptr;
    std::vector<std::string> adapters_{};
    wxTimer redrawTimer_{};
    wxTimer sessionTimer_{};
};

}

wxWindow* BuildTerminalPage(wxWindow* parent, deskhub::ui::UiSettings& settings,
    std::function<void()> onSettingsChanged) {
    return new TerminalPanel(parent, settings, std::move(onSettingsChanged));
}

void ShutdownTerminalPage(wxWindow* page) {
    if (auto* panel = dynamic_cast<TerminalPanel*>(page)) panel->Shutdown();
}

void StartTerminalAutoShare(wxWindow* page) {
    if (auto* panel = dynamic_cast<TerminalPanel*>(page)) panel->StartAutoShare();
}
