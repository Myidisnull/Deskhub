#pragma once
#include <gtk/gtk.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "deskhub/ui/TransferView.h"
#include "deskhubp/net/UdpSocket.h"

class FileSendTarget {
public:
    virtual ~FileSendTarget() = default;
    virtual bool Begin(const std::vector<std::filesystem::path>& files) = 0;
    virtual void Cancel() = 0;
    virtual deskhub::ui::TransferView View() const = 0;
    virtual std::string LastError() const = 0;
};

struct FileSendHooks {
    std::function<bool(const std::vector<std::filesystem::path>&)> begin;
    std::function<void()> cancel;
    std::function<deskhub::ui::TransferView()> view;
    std::function<std::string()> error;
};

std::unique_ptr<FileSendTarget> MakeStandaloneFileSendTarget(const NetAddr& server,
    const std::string& address, const std::string& passcode, const std::string& clientName);

std::unique_ptr<FileSendTarget> MakeHookedFileSendTarget(FileSendHooks hooks);

void OpenFileSendWindow(GtkWindow* parent, const std::string& subtitle,
    std::unique_ptr<FileSendTarget> target);
