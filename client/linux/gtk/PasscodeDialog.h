#pragma once
#include <gtk/gtk.h>

#include <string>

bool ShowPasscodeDialog(GtkWindow* parent, const std::string& addr, std::string& passcode);
