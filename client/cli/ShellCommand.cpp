#include "Commands.h"

#include <chrono>
#include <cstdio>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "Output.h"
#include "Passcode.h"
#include "Signals.h"

#include "deskhub/ui/Strings.h"
#include "deskhubp/client/TerminalViewer.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/system/Console.h"
#include "deskhubp/system/UiSettingsStore.h"

namespace deskhubcli {

namespace {

constexpr uint32_t kReadTimeoutMs = 50;
constexpr size_t kSendChunkBytes = 256;

void WriteThrough(std::span<const uint8_t> bytes) {
    std::fwrite(bytes.data(), 1, bytes.size(), stdout);
    std::fflush(stdout);
}

deskhub::TermSize SizeNow() {
    const deskhubp::ConsoleSize console = deskhubp::ConsoleSizeNow();
    deskhub::TermSize size;
    size.cols = console.columns;
    size.rows = console.rows;
    return size;
}

bool Finished(deskhubp::TerminalViewerState state) {
    return state == deskhubp::TerminalViewerState::Refused ||
           state == deskhubp::TerminalViewerState::Failed ||
           state == deskhubp::TerminalViewerState::Ended;
}

ExitCode CodeFor(deskhubp::TerminalViewerState state) {
    if (state == deskhubp::TerminalViewerState::Refused) return ExitCode::Refused;
    if (state == deskhubp::TerminalViewerState::Failed) return ExitCode::Unreachable;
    return ExitCode::Ok;
}

}

ExitCode RunShell(const Command& command) {
    NetAddr host{};
    if (!ParseNetAddr(command.address, host)) {
        PrintError(deskhub::ui::InvalidAddressLine(command.address));
        PrintError(deskhub::ui::InvalidAddressHint());
        return ExitCode::Usage;
    }

    const Passcode passcode = ResolvePasscode(command);
    if (!passcode.ok) {
        PrintError(passcode.error);
        return ExitCode::Usage;
    }

    deskhubp::TerminalViewerConfig config;
    config.host = host;
    config.hostLabel = command.address;
    config.passcode = passcode.value;
    config.clientName =
        command.deviceName ? *command.deviceName : deskhubp::SessionDeviceName();
    config.size = SizeNow();

    deskhubp::TerminalViewerCallbacks hooks;
    hooks.onOutput = [](std::span<const uint8_t> bytes) { WriteThrough(bytes); };
    hooks.onState = [&command](deskhubp::TerminalViewerState state, std::string_view message) {
        if (command.quiet || message.empty()) return;
        if (state == deskhubp::TerminalViewerState::Live) return;
        PrintError(message);
    };
    hooks.onTrustAsked = [](deskhub::TrustVerdict, std::string_view) {};

    deskhubp::TerminalViewer viewer;
    if (!viewer.Start(config, std::move(hooks))) {
        PrintError(viewer.Message().empty() ? "Could not open the shell." : viewer.Message());
        return ExitCode::Unreachable;
    }

    WatchForInterrupt();
    deskhubp::RawConsole raw;
    while (!Interrupted() && !Finished(viewer.State())) {
        if (deskhubp::ConsoleResized()) viewer.Resize(SizeNow());

        std::string typed;
        typed.reserve(kSendChunkBytes);
        for (;;) {
            const int byte = deskhubp::ReadStdinByte(typed.empty() ? kReadTimeoutMs : 0);
            if (byte == deskhubp::kStdinTimedOut || byte == deskhubp::kStdinClosed) break;
            typed.push_back(char(byte));
            if (typed.size() >= kSendChunkBytes) break;
        }
        if (!typed.empty()) viewer.SendText(typed);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const deskhubp::TerminalViewerState last = viewer.State();
    viewer.Stop();
    return Interrupted() ? ExitCode::Interrupted : CodeFor(last);
}

}
