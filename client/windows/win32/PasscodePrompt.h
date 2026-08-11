#pragma once
#include <string>

class wxWindow;

bool ShowPasscodePrompt(wxWindow* parent, std::string& addr, std::string& passcode);
