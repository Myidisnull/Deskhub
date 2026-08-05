#pragma once
#include <string>

class wxWindow;

bool ShowPasscodePrompt(wxWindow* parent, const std::string& addr, std::string& passcode);
