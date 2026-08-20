#include "Commands.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "Output.h"
#include "Passcode.h"
#include "Signals.h"

#include "deskhub/ui/Strings.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/TerminalViewer.h"
#include "deskhubp/system/Console.h"
#include "deskhubp/system/DeviceName.h"
#include "deskhubp/system/HostIdentity.h"
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

ExitCode CodeFor(deskhubp::TerminalViewerState state, bool keyChanged) {
    if (keyChanged) return ExitCode::KeyChanged;
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

    if (!deskhubp::QuicAvailable()) {
        PrintError(deskhub::ui::kShareNoQuicLibrary);
        return ExitCode::Unsupported;
    }

    deskhubp::TerminalViewerConfig config;
    config.host = host;
    config.hostLabel = command.address;
    config.passcode = passcode.value;
    config.clientName =
        command.deviceName ? *command.deviceName : deskhubp::SessionDeviceName();
    config.size = SizeNow();

    std::atomic<bool> keyChanged{false};

    deskhubp::TerminalViewerCallbacks hooks;
    hooks.onOutput = [](std::span<const uint8_t> bytes) { WriteThrough(bytes); };
    hooks.onState = [&command](deskhubp::TerminalViewerState state, std::string_view message) {
        if (command.quiet || message.empty()) return;
        if (state == deskhubp::TerminalViewerState::Live) return;
        PrintError(message);
    };
    hooks.onTrustAsked = [&keyChanged](deskhub::TrustVerdict verdict,
                             std::string_view fingerprint) {
        if (verdict != deskhub::TrustVerdict::Changed) return;
        keyChanged.store(true);
        PrintError(deskhub::ui::kTrustChangedTitle);
        PrintError(deskhub::ui::kTrustChangedBody);
        PrintError(std::string(deskhub::ui::kTrustFingerprintLabel) + " " +
                   std::string(fingerprint));
    };

    WatchForInterrupt();

    deskhubp::TerminalViewer viewer;
    if (!viewer.Start(config, std::move(hooks))) {
        PrintError(deskhub::ui::CouldNotConnectTo(command.address));
        return ExitCode::Unreachable;
    }

    const deskhubp::RawConsole raw;
    std::string pending;
    bool typingDone = false;

    while (!Interrupted() && !keyChanged.load() && !Finished(viewer.State())) {
        if (deskhubp::ConsoleResized()) viewer.Resize(SizeNow());

        if (viewer.State() != deskhubp::TerminalViewerState::Live) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kReadTimeoutMs));
            continue;
        }

        if (typingDone) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kReadTimeoutMs));
            continue;
        }

        const int byte = deskhubp::ReadStdinByte(kReadTimeoutMs);
        if (byte == deskhubp::kStdinClosed) {
            typingDone = true;
        }
        if (byte < 0) {
            if (!pending.empty()) {
                viewer.SendText(pending);
                pending.clear();
            }
            continue;
        }

        pending.push_back(char(byte));
        if (pending.size() < kSendChunkBytes) continue;
        viewer.SendText(pending);
        pending.clear();
    }

    if (!pending.empty()) viewer.SendText(pending);

    const deskhubp::TerminalViewerState last = viewer.State();
    viewer.Stop();
    if (!command.quiet) PrintError("");
    return CodeFor(last, keyChanged.load());
}

}
