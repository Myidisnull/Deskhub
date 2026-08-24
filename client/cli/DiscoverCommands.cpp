#include "Commands.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Output.h"
#include "Passcode.h"

#include "deskhub/cli/Json.h"
#include "deskhub/media/SourceLabel.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/media/DisplayEnum.h"
#include "deskhubp/client/HostProbe.h"
#include "deskhubp/client/LanScanner.h"
#include "deskhubp/client/SourceQuery.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/system/HostIdentity.h"

namespace deskhubcli {

namespace {

bool ResolveTarget(const Command& command, NetAddr& out) {
    if (ParseNetAddr(command.address, out)) return true;
    PrintError(deskhub::ui::InvalidAddressLine(command.address));
    PrintError(deskhub::ui::InvalidAddressHint());
    return false;
}

bool TakePasscode(const Command& command, std::string& out) {
    const Passcode passcode = ResolvePasscode(command);
    if (!passcode.ok) {
        PrintError(passcode.error);
        return false;
    }
    out = passcode.value;
    return true;
}

}

ExitCode RunDisplays(const Command& command) {
    if (command.forget) {
        deskhubp::ForgetDisplaySelection();
        if (!command.quiet) PrintLine("Forgot the saved screen choice - the next listing asks again.");
        return ExitCode::Ok;
    }

    const std::vector<deskhub::media::ShareSource> displays = deskhubp::ListDisplays();
    if (displays.empty()) {
        const std::string reason = deskhubp::ListDisplaysError();
        PrintError(reason.empty() ? std::string(deskhub::ui::kNoDisplayFound)
                                  : std::string(deskhub::ui::kNoDisplayFound) + " " + reason);
        deskhubp::ReleaseDisplays();
        return ExitCode::NothingToShare;
    }

    if (command.json) {
        deskhub::cli::JsonWriter json;
        json.ArrayBegin();
        for (size_t i = 0; i < displays.size(); ++i) {
            const deskhub::media::ShareSource& display = displays[i];
            json.ObjectBegin();
            json.Field("id", i);
            json.Field("name", deskhub::media::SourceName(display.name, uint8_t(i)));
            json.Field("width", display.width);
            json.Field("height", display.height);
            json.Field("x", display.x);
            json.Field("y", display.y);
            json.ObjectEnd();
        }
        json.ArrayEnd();
        PrintLine(json.Text());
    } else {
        Table table;
        table.Row({"ID", "NAME", "SIZE", "POSITION"});
        for (size_t i = 0; i < displays.size(); ++i) {
            const deskhub::media::ShareSource& display = displays[i];
            table.Row({std::to_string(i), deskhub::media::SourceName(display.name, uint8_t(i)),
                deskhub::media::SourceSizeLabel(display.width, display.height),
                std::to_string(display.x) + "," + std::to_string(display.y)});
        }
        table.Print();
    }

    deskhubp::ReleaseDisplays();
    return ExitCode::Ok;
}

ExitCode RunScan(const Command& command) {
    std::mutex mutex;
    std::condition_variable finished;
    std::vector<deskhubp::ScanHit> hits;
    deskhubp::ScanProgress summary;
    bool done = false;

    deskhubp::LanScanner scanner;
    const bool started = scanner.Start(
        command.port, [](std::function<void()> work) { work(); },
        [&](const deskhubp::ScanHit& hit) {
            const std::lock_guard<std::mutex> lock(mutex);
            hits.push_back(hit);
        },
        [&](const deskhubp::ScanProgress& progress) {
            if (command.quiet || command.json) return;
            PrintError(deskhub::ui::ScanningStatus(progress.probed, progress.total, command.port));
        },
        [&](const deskhubp::ScanProgress& progress) {
            const std::lock_guard<std::mutex> lock(mutex);
            summary = progress;
            done = true;
            finished.notify_all();
        });

    if (!started) {
        PrintError("a scan is already running");
        return ExitCode::Failed;
    }

    std::unique_lock<std::mutex> lock(mutex);
    finished.wait(lock, [&] { return done; });

    if (command.json) {
        deskhub::cli::JsonWriter json;
        json.ObjectBegin();
        json.FieldBegin("hosts");
        json.ArrayBegin();
        for (const deskhubp::ScanHit& hit : hits) {
            json.ObjectBegin();
            json.Field("address", hit.addr);
            json.Field("rttMs", hit.rttMs);
            json.ObjectEnd();
        }
        json.ArrayEnd();
        json.Field("checked", summary.total);
        json.Field("found", summary.found);
        json.ObjectEnd();
        PrintLine(json.Text());
        return ExitCode::Ok;
    }

    if (!hits.empty()) {
        Table table;
        table.Row({"ADDRESS", "PING"});
        for (const deskhubp::ScanHit& hit : hits)
            table.Row({hit.addr, std::to_string(hit.rttMs) + " ms"});
        table.Print();
    }
    if (!command.quiet) PrintError(deskhub::ui::ScanFinishedStatus(summary.found, summary.total));
    return ExitCode::Ok;
}

ExitCode RunSources(const Command& command) {
    NetAddr server{};
    if (!ResolveTarget(command, server)) return ExitCode::Usage;

    std::string passcode;
    if (!TakePasscode(command, passcode)) return ExitCode::Usage;

    if (!deskhubp::QuicAvailable()) {
        PrintError(deskhub::ui::kShareNoQuicLibrary);
        return ExitCode::Unsupported;
    }

    if (!deskhubp::ProbeHostRttMs(server, command.timeoutMs)) {
        PrintError(deskhub::ui::SourceQueryFailed(command.address));
        return ExitCode::Unreachable;
    }

    std::vector<deskhub::SourceInfo> sources;
    deskhub::AuthResultCode code = deskhub::AuthResultCode::NotPaired;
    deskhub::HostCaps caps{};
    if (!QuerySources(server, sources, passcode, &code, &caps)) {
        PrintError(deskhub::ui::AuthRefusalText(code));
        return ExitCode::Refused;
    }
    if (sources.empty() && !caps.terminal && !caps.files) {
        PrintError(deskhub::ui::SourceQueryEmpty(command.address));
        return ExitCode::Unreachable;
    }

    if (command.json) {
        deskhub::cli::JsonWriter json;
        json.ObjectBegin();
        json.Field("address", command.address);
        json.FieldBegin("sources");
        json.ArrayBegin();
        for (const deskhub::SourceInfo& source : sources) {
            json.ObjectBegin();
            json.Field("id", source.sourceId);
            json.Field("name", deskhub::media::SourceName(source.name, source.sourceId));
            json.Field("width", source.width);
            json.Field("height", source.height);
            json.ObjectEnd();
        }
        json.ArrayEnd();
        json.FieldBegin("caps");
        json.ObjectBegin();
        json.Field("acceptsInput", caps.acceptsInput);
        json.Field("terminal", caps.terminal);
        json.Field("audio", caps.audio);
        json.Field("files", caps.files);
        json.ObjectEnd();
        json.ObjectEnd();
        PrintLine(json.Text());
        return ExitCode::Ok;
    }

    if (sources.empty()) {
        PrintLine(deskhub::ui::NoDisplaySharedNote(caps.terminal, caps.files));
    } else {
        Table table;
        table.Row({"ID", "NAME", "SIZE"});
        for (const deskhub::SourceInfo& source : sources)
            table.Row({std::to_string(unsigned(source.sourceId)),
                deskhub::media::SourceName(source.name, source.sourceId),
                deskhub::media::SourceSizeLabel(source.width, source.height)});
        table.Print();
    }

    if (!command.quiet) {
        const auto yesNo = [](bool value) { return value ? "yes" : "no"; };
        PrintLine("");
        PrintLine(std::string("accepts input: ") + yesNo(caps.acceptsInput));
        PrintLine(std::string("remote shell:  ") + yesNo(caps.terminal));
        PrintLine(std::string("audio:         ") + yesNo(caps.audio));
        PrintLine(std::string("file transfer: ") + yesNo(caps.files));
    }
    return ExitCode::Ok;
}

ExitCode RunProbe(const Command& command) {
    NetAddr host{};
    if (!ResolveTarget(command, host)) return ExitCode::Usage;

    const std::optional<uint32_t> rttMs = deskhubp::ProbeHostRttMs(host, command.timeoutMs);

    if (command.json) {
        deskhub::cli::JsonWriter json;
        json.ObjectBegin();
        json.Field("address", command.address);
        json.Field("reachable", rttMs.has_value());
        json.Field("rttMs", rttMs ? int64_t(*rttMs) : int64_t(-1));
        json.ObjectEnd();
        PrintLine(json.Text());
        return rttMs ? ExitCode::Ok : ExitCode::Unreachable;
    }

    if (!rttMs) {
        PrintError(deskhub::ui::CouldNotConnectTo(command.address));
        return ExitCode::Unreachable;
    }
    PrintLine(command.address + "  " + std::to_string(*rttMs) + " ms");
    return ExitCode::Ok;
}

}
