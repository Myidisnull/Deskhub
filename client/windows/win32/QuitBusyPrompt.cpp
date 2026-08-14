#include <wx/wx.h>
#include <wx/artprov.h>

#include "QuitBusyPrompt.h"
#include "AppTheme.h"

#include "deskhub/ui/Strings.h"

namespace {

namespace ui = deskhub::ui;

wxString ToWx(const char* s) {
    return wxString::FromUTF8(s);
}

class QuitBusyPrompt final : public wxDialog {
public:
    explicit QuitBusyPrompt(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, ToWx(ui::kAppTitle), wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_DIALOG_STYLE) {
        const AppTheme& theme = AppThemeCurrent();
        SetBackgroundColour(theme.dialogBg);
        SetForegroundColour(theme.headingText);

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int edge = FromDIP(20);
        const int gap = FromDIP(12);

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* icon = new wxStaticBitmap(this, wxID_ANY,
            wxArtProvider::GetBitmap(wxART_WARNING, wxART_MESSAGE_BOX, FromDIP(wxSize(32, 32))));
        icon->SetBackgroundColour(theme.dialogBg);
        row->Add(icon, wxSizerFlags().Top().Border(wxRIGHT, gap));

        auto* message = new wxStaticText(this, wxID_ANY, ToWx(ui::kQuitWhileBusyMessage));
        message->Wrap(FromDIP(360));
        message->SetForegroundColour(theme.headingText);
        message->SetBackgroundColour(theme.dialogBg);
        row->Add(message, wxSizerFlags(1).CentreVertical());
        sizer->Add(row, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, edge));

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        auto* cancel = new wxButton(this, wxID_CANCEL, ToWx(ui::kQuitWhileBusyCancel),
            wxDefaultPosition, FromDIP(wxSize(96, 32)));
        auto* quit = new wxButton(this, wxID_YES, ToWx(ui::kQuitWhileBusyQuit),
            wxDefaultPosition, FromDIP(wxSize(96, 32)));
        cancel->SetBackgroundColour(theme.buttonBg);
        cancel->SetForegroundColour(theme.headingText);
        cancel->SetOwnBackgroundColour(theme.buttonBg);
        cancel->SetOwnForegroundColour(theme.headingText);
        quit->SetBackgroundColour(theme.dangerBg);
        quit->SetForegroundColour(*wxWHITE);
        quit->SetOwnBackgroundColour(theme.dangerBg);
        quit->SetOwnForegroundColour(*wxWHITE);
        quit->SetFont(quit->GetFont().Bold());
        cancel->SetDefault();
        buttons->Add(cancel);
        buttons->Add(quit, wxSizerFlags().Border(wxLEFT, gap));
        sizer->Add(buttons, wxSizerFlags().Right().Border(wxALL, edge));

        SetSizerAndFit(sizer);
        CentreOnParent();
    }
};

}

bool ShowQuitBusyPrompt(wxWindow* parent) {
    QuitBusyPrompt prompt(parent);
    return prompt.ShowModal() == wxID_YES;
}
