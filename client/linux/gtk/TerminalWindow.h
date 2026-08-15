#pragma once
#include <gtk/gtk.h>

#include <string>

struct TerminalLaunch {
    std::string address{};
    std::string passcode{};
    std::string clientName{};
};

bool OpenTerminalWindow(GtkWindow* parent, const TerminalLaunch& launch);
