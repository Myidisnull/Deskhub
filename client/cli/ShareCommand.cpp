#include "Commands.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "Output.h"
#include "Passcode.h"
#include "Signals.h"

#include "deskhub/cli/Json.h"
#include "deskhub/media/SourceLabel.h"
#include "deskhub/session/ShareFlow.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/media/DisplayEnum.h"
#include "deskhubp/session/AgentLoop.h"
#include "deskhubp/session/PairingAskQueue.h"
#include "deskhubp/system/DeviceName.h"
#include "deskhubp/system/FileStore.h"
#include "deskhubp/system/UiSettingsStore.h"
#include "deskhubp/net/UdpSocket.h"

#include <optional>

namespace deskhubcli {

namespace {

using deskhub::cli::PairingPolicy;
using ShareSource = deskhub::media::ShareSource;

constexpr uint32_t kPollMs = 200;

std::filesystem::path TransferFolder(const std::optional<std::string>& chosen) {
    if (!chosen || chosen->empty()) return deskhubp::DefaultTransferDir();
    const std::u8string wide(chosen->begin(), chosen->end());
    return std::filesystem::path(wide);
}

std::string ViewerSummary(const deskhub::media::AgentSourceStatus& source) {
    if (!source.viewerCount) return "-";
    std::string summary = deskhub::media::ViewerCountLabel(source.viewerCount);
    if (!source.viewerAddr.empty()) summary += " (" + source.viewerAddr + ")";
    return summary;
}

void PrintStatusTable(const std::vector<deskhub::media::AgentSourceStatus>& sources,
    size_t shellCount) {
    Table table;
    table.Row({"SOURCE", "SIZE", "FPS", "KBPS", "PING", "VIEWERS"});
    for (const deskhub::media::AgentSourceStatus& source : sources)
        table.Row({deskhub::media::SourceName(source.name, source.sourceId),
            deskhub::media::SourceSizeLabel(source.width, source.height),
            std::to_string(int(source.sendFps + 0.5)), std::to_string(int(source.sendKbps + 0.5)),
            source.rttMs ? std::to_string(source.rttMs) + " ms" : std::string("-"),
            ViewerSummary(source)});
    if (shellCount)
        table.Row({"Terminal", "-", "-", "-", "-",
            std::to_string(shellCount) + (shellCount == 1 ? " shell" : " shells")});
    table.Print();
}

void PrintStatusJson(const std::vector<deskhub::media::AgentSourceStatus>& sources,
    size_t shellCount) {
    deskhub::cli::JsonWriter json;
    json.ObjectBegin();
    json.FieldBegin("sources");
    json.ArrayBegin();
    for (const deskhub::media::AgentSourceStatus& source : sources) {
        json.ObjectBegin();
        json.Field("id", source.sourceId);
        json.Field("name", deskhub::media::SourceName(source.name, source.sourceId));
        json.Field("width", source.width);
        json.Field("height", source.height);
        json.Field("sendFps", int64_t(source.sendFps + 0.5));
        json.Field("sendKbps", int64_t(source.sendKbps + 0.5));
        json.Field("rttMs", source.rttMs);
        json.Field("viewers", source.viewerCount);
        json.ObjectEnd();
    }
    json.ArrayEnd();
    json.Field("shells", int64_t(shellCount));
    json.ObjectEnd();
    PrintLine(json.Text());
}

void AnswerPairing(PairingPolicy policy, bool quiet) {
    for (const deskhubp::PairingAsk& ask : deskhubp::SharedPairingAskQueue().Take(8)) {
        const bool allowed = policy == PairingPolicy::Allow;
        if (!quiet) {
            PrintError(deskhub::ui::PairingRequestBody(ask.name,
                NetAddr::Unpack(ask.addrPacked).ToString(), ask.shortKey));
            PrintError(allowed ? "Letting it in." : "Turning it away.");
        }
        deskhubp::SharedPairingAskQueue().Answer(ask.addrPacked, allowed);
    }
}

bool CollectSources(const Command& command, std::vector<ShareSource>& out) {
    std::vector<ShareSource> displays = deskhubp::ListDisplays();
    if (displays.empty()) {
        const std::string reason = deskhubp::ListDisplaysError();
        if (reason == deskhubp::kListDisplaysCancelled) {
            PrintError("No display was picked, so there is nothing to share.");
            return false;
        }
        PrintError(reason.empty() ? std::string(deskhub::ui::kNoDisplayFound) : reason);
        return false;
    }

    const deskhub::cli::DisplayPick pick =
        deskhub::cli::PickDisplays(command.share.displays, displays);
    if (!pick.error.empty()) {
        PrintError(pick.error);
        return false;
    }

    std::vector<ShareSource> chosen;
    chosen.reserve(pick.indices.size());
    for (size_t index : pick.indices) chosen.push_back(displays[index]);

    const deskhub::ShareClampResult clamp = deskhub::ClampShareSources(std::move(chosen));
    if (clamp.clamped) PrintError(deskhub::ui::ShareClampWarning());
    out = clamp.sources;
    return true;
}

}

ExitCode RunShare(const Command& command) {
    const Passcode passcode = ResolvePasscode(command);
    if (!passcode.ok) {
        PrintError(passcode.error);
        return ExitCode::Usage;
    }
    if (command.share.pairing == PairingPolicy::Ask) {
        PrintError("--pairing ask is not built yet - use allow or deny.");
        return ExitCode::Usage;
    }

    deskhub::ui::UiSettings settings =
        deskhub::cli::ApplyShareOptions(command, deskhubp::LoadUiSettings());
    if (!passcode.value.empty()) settings.passcode = passcode.value;
    if (settings.deviceName.empty()) settings.deviceName = deskhubp::LocalDeviceName();
    settings.shareTerminal = command.share.terminal;
    settings.acceptFiles = command.share.files;
    if (command.share.allowNewPairings)
        settings.allowNewPairings = *command.share.allowNewPairings;
    else if (command.share.pairing == PairingPolicy::Deny)
        settings.allowNewPairings = false;
    else if (command.share.pairing == PairingPolicy::Allow)
        settings.allowNewPairings = true;
    deskhubp::SaveUiSettings(settings);

    deskhub::media::AgentOptions options = deskhub::ShareOptionsOf(settings);
    options.shareTerminal = command.share.terminal;
    options.acceptFiles = command.share.files;
    options.clipboardSync = false;

    const bool sharesTenant = command.share.terminal || command.share.files;
    std::vector<ShareSource> sources;
    if (command.share.screen && !CollectSources(command, sources)) {
        if (!sharesTenant) return ExitCode::NothingToShare;
        sources.clear();
    }
    if (sources.empty() && !sharesTenant) return ExitCode::NothingToShare;

    if (command.share.files) {
        const std::filesystem::path folder = TransferFolder(command.share.filesDir);
        deskhubp::FileStore store;
        if (!store.SetDirectory(folder)) {
            PrintError(std::string(deskhub::ui::kShareStartFailed) +
                       ": files cannot be stored in " + deskhubp::PathText(folder) + ".");
            return ExitCode::Failed;
        }
    }

    WatchForInterrupt();

    AgentLoop host;
    if (!host.Start(sources, options)) {
        PrintError(std::string(deskhub::ui::kShareStartFailed) + ".");
        return ExitCode::BindFailed;
    }

    PrintError("Listening on UDP port " + std::to_string(options.port) + ".");

    const bool screenSharing = !sources.empty();
    if (!command.quiet) {
        PrintError(deskhub::ui::ShareSummaryLine(screenSharing, command.share.terminal,
            command.share.files, options.port));
        PrintError(deskhub::ui::PasscodeNote(options.passcode));
        if (!options.allowInput) PrintError(deskhub::ui::kViewOnlyNote);
        if (command.share.files)
            PrintError(deskhub::ui::TransferFolderNote(
                deskhubp::PathText(TransferFolder(command.share.filesDir))));
        PrintError("Press Ctrl-C to stop sharing.");
    }

    uint32_t sinceStatusMs = 0;
    while (!Interrupted() && host.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        AnswerPairing(command.share.pairing, command.quiet);

        if (!command.share.status || command.quiet) continue;
        sinceStatusMs += kPollMs;
        if (sinceStatusMs < command.share.statusIntervalMs) continue;
        sinceStatusMs = 0;

        const std::vector<deskhub::media::AgentSourceStatus> status = host.Status();
        const size_t shellCount = host.TerminalSessions().size();
        if (command.json) {
            PrintStatusJson(status, shellCount);
        } else if (!status.empty() || shellCount) {
            PrintStatusTable(status, shellCount);
        }
    }

    host.Stop();
    if (!command.quiet) PrintError("Stopped sharing.");
    return ExitCode::Ok;
}

}
