#include "Commands.h"

#include <chrono>
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
#include "deskhubp/session/TerminalHost.h"
#include "deskhubp/system/DeviceName.h"
#include "deskhubp/system/UiSettingsStore.h"

namespace deskhubcli {

namespace {

using deskhub::cli::PairingPolicy;

constexpr uint32_t kPollMs = 200;

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

void AnswerPairing(AgentLoop& agent, PairingPolicy policy, bool quiet) {
    for (const PairingRequest& request : agent.TakePairingRequests()) {
        const NetAddr peer = NetAddr::Unpack(request.addrPacked);
        const bool allowed = policy == PairingPolicy::Allow;
        if (!quiet) {
            PrintError(deskhub::ui::PairingRequestBody(request.name, peer.ToString(),
                request.shortKey));
            PrintError(allowed ? "Letting it in." : "Turning it away.");
        }
        agent.AnswerPairing(request.addrPacked, allowed);
    }
}

bool CollectSources(const Command& command, std::vector<AgentSource>& out) {
    std::vector<AgentSource> displays = deskhubp::ListDisplays();
    if (displays.empty()) {
        const std::string reason = deskhubp::ListDisplaysError();
        if (reason == deskhubp::kListDisplaysCancelled) {
            PrintError("No display was picked, so there is nothing to share.");
            return false;
        }
        PrintError(reason.empty() ? std::string(deskhub::ui::kNoDisplayFound)
                                  : std::string(deskhub::ui::kCaptureUnavailableTitle) + ": " + reason);
        return false;
    }

    const deskhub::cli::DisplayPick pick =
        deskhub::cli::PickDisplays(command.share.displays, displays);
    if (!pick.error.empty()) {
        PrintError(pick.error);
        return false;
    }

    std::vector<AgentSource> chosen;
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

    AgentOptions options = deskhub::ShareOptionsOf(settings, command.share.terminal);
    options.clipboardSync = false;

    std::vector<AgentSource> sources;
    if (command.share.screen && !CollectSources(command, sources)) {
        if (!command.share.terminal) return ExitCode::NothingToShare;
        sources.clear();
    }
    if (sources.empty() && !command.share.terminal) return ExitCode::NothingToShare;

    WatchForInterrupt();

    AgentLoop agent;
    deskhubp::TerminalHost terminal;
    if (!agent.Start(sources, options)) {
        PrintError(std::string(deskhub::ui::kShareStartFailed) + ": " + agent.LastError());
        return ExitCode::BindFailed;
    }

    if (command.share.terminal) {
        if (!terminal.Start(agent.Socket(), std::string(), deskhubp::TerminalHostCallbacks{})) {
            PrintError(std::string(deskhub::ui::kShareStartFailed) + ": the shell would not open.");
            agent.Stop();
            return ExitCode::Failed;
        }
        agent.SetTerminal(&terminal);
    }

    const bool screenSharing = !agent.Status().empty();
    if (!command.quiet) {
        PrintError(deskhub::ui::ShareSummaryLine(screenSharing, terminal.Running(), options.port));
        PrintError(deskhub::ui::PasscodeNote(options.passcode));
        if (!options.allowInput) PrintError(deskhub::ui::kViewOnlyNote);
        const std::string bindWarning = agent.BindWarning();
        if (!bindWarning.empty()) PrintError(bindWarning);
        PrintError("Press Ctrl-C to stop sharing.");
    }

    uint32_t sinceStatusMs = 0;
    while (!Interrupted() && agent.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        AnswerPairing(agent, command.share.pairing, command.quiet);

        if (!command.share.status || command.quiet) continue;
        sinceStatusMs += kPollMs;
        if (sinceStatusMs < command.share.statusIntervalMs) continue;
        sinceStatusMs = 0;

        const std::vector<AgentSourceStatus> status = agent.Status();
        const size_t shellCount = terminal.SessionCount();
        if (command.json) {
            PrintStatusJson(status, shellCount);
        } else if (!status.empty() || shellCount) {
            PrintStatusTable(status, shellCount);
        }
    }

    if (terminal.Running()) terminal.Stop();
    agent.Stop();
    if (!command.quiet) PrintError("Stopped sharing.");
    return ExitCode::Ok;
}

}
