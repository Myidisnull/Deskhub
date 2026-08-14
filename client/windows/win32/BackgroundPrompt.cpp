#include <wx/wx.h>

#include "BackgroundPrompt.h"
#include "AppTheme.h"

#include "deskhub/ui/Strings.h"

namespace {

namespace ui = deskhub::ui;

wxString ToWx(const char* s) {
    return wxString::FromUTF8(s);
}

void StyleDialogChrome(wxWindow* root) {
    const AppTheme& theme = AppThemeCurrent();
    root->SetBackgroundColour(theme.dialogBg);
    root->SetForegroundColour(theme.headingText);
}

void StyleLabel(wxStaticText* label, const wxColour& colour) {
    label->SetForegroundColour(colour);
    label->SetBackgroundColour(AppThemeCurrent().dialogBg);
}

void StyleSecondaryButton(wxButton* button) {
    const AppTheme& theme = AppThemeCurrent();
    button->SetBackgroundColour(theme.buttonBg);
    button->SetForegroundColour(theme.headingText);
    button->SetOwnBackgroundColour(theme.buttonBg);
    button->SetOwnForegroundColour(theme.headingText);
}

void StylePrimaryButton(wxButton* button) {
    const AppTheme& theme = AppThemeCurrent();
    button->SetBackgroundColour(theme.accent);
    button->SetForegroundColour(*wxWHITE);
    button->SetOwnBackgroundColour(theme.accent);
    button->SetOwnForegroundColour(*wxWHITE);
    button->SetFont(button->GetFont().Bold());
}

class BackgroundPrompt final : public wxDialog {
public:
    explicit BackgroundPrompt(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, ToWx(ui::kBackgroundPromptTitle), wxDefaultPosition,
              wxDefaultSize, wxDEFAULT_DIALOG_STYLE) {
        StyleDialogChrome(this);

        const AppTheme& theme = AppThemeCurrent();
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int edge = FromDIP(20);
        const int gap = FromDIP(12);

        auto* message = new wxStaticText(this, wxID_ANY, ToWx(ui::kBackgroundPromptMessage));
        message->Wrap(FromDIP(380));
        StyleLabel(message, theme.headingText);
        wxFont messageFont = message->GetFont();
        messageFont.SetPointSize(messageFont.GetPointSize() + 1);
        message->SetFont(messageFont);
        sizer->Add(message, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, edge));

        auto* choices = new wxBoxSizer(wxVERTICAL);
        yes_ = new wxRadioButton(this, wxID_ANY, ToWx(ui::kBackgroundPromptYes),
            wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
        no_ = new wxRadioButton(this, wxID_ANY, ToWx(ui::kBackgroundPromptNo));
        yes_->SetValue(true);
        yes_->SetForegroundColour(theme.headingText);
        no_->SetForegroundColour(theme.headingText);
        yes_->SetBackgroundColour(theme.dialogBg);
        no_->SetBackgroundColour(theme.dialogBg);
        choices->Add(yes_, wxSizerFlags().Border(wxBOTTOM, FromDIP(8)));
        choices->Add(no_);
        sizer->Add(choices, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, edge));

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        auto* close = new wxButton(this, wxID_CANCEL, ToWx(ui::kBackgroundPromptClose),
            wxDefaultPosition, FromDIP(wxSize(96, 32)));
        auto* confirm = new wxButton(this, wxID_OK, ToWx(ui::kBackgroundPromptConfirm),
            wxDefaultPosition, FromDIP(wxSize(96, 32)));
        StyleSecondaryButton(close);
        StylePrimaryButton(confirm);
        confirm->SetDefault();
        buttons->Add(close);
        buttons->Add(confirm, wxSizerFlags().Border(wxLEFT, gap));
        sizer->Add(buttons, wxSizerFlags().Right().Border(wxALL, edge));

        SetSizerAndFit(sizer);
        CentreOnParent();
    }

    bool runInBackground() const {
        return yes_->GetValue();
    }

private:
    wxRadioButton* yes_ = nullptr;
    wxRadioButton* no_ = nullptr;
};

}

BackgroundPromptResult ShowBackgroundPrompt(wxWindow* parent, bool& runInBackground) {
    BackgroundPrompt prompt(parent);
    if (prompt.ShowModal() != wxID_OK) return BackgroundPromptResult::kClose;
    runInBackground = prompt.runInBackground();
    return BackgroundPromptResult::kConfirm;
}
