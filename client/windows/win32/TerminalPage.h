#pragma once
#include <wx/wx.h>

#include <functional>

#include "deskhub/ui/UiSettings.h"

wxWindow* BuildTerminalPage(wxWindow* parent, deskhub::ui::UiSettings& settings,
    std::function<void()> onSettingsChanged);
void ShutdownTerminalPage(wxWindow* page);
void StartTerminalAutoShare(wxWindow* page);
