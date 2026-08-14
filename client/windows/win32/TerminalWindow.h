#pragma once
#include <wx/wx.h>

#include <string>

struct TerminalLaunch {
    std::string address{};
    std::string passcode{};
    std::string clientName{};
};

bool OpenTerminalWindow(wxWindow* parent, const TerminalLaunch& launch);
