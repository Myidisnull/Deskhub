#pragma once
#include <gtk/gtk.h>

#include <string>

bool ShowPasscodeDialog(GtkWindow* parent, std::string& addr, std::string& passcode);
