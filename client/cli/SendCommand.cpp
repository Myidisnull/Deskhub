#include "Commands.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Output.h"
#include "Passcode.h"
#include "Signals.h"

#include "deskhub/cli/Json.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/client/FileTransferClient.h"
#include "deskhubp/net/HostProbe.h"
#include "deskhubp/net/SourceQuery.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/system/UiSettingsStore.h"

namespace deskhubcli {

namespace {

constexpr uint32_t kPollMs = 100;

std::filesystem::path PathOf(const std::string& text) {
    const std::u8string wide(text.begin(), text.end());
    return std::filesystem::path(wide);
}

ExitCode CodeFor(deskhubp::FileTransferClientState state) {
    switch (state) {
        case deskhubp::FileTransferClientState::Done: return ExitCode::Ok;
        case deskhubp::FileTransferClientState::Refused: return ExitCode::Refused;
        case deskhubp::FileTransferClientState::Failed: return ExitCode::Unreachable;
        default: break;
    }
    return ExitCode::Failed;
}

void PrintResultJson(const deskhubp::FileTransferClient& client, size_t fileCount) {
    deskhub::cli::JsonWriter json;
    json.ObjectBegin();
    json.Field("sent", client.State() == deskhubp::FileTransferClientState::Done);
    json.Field("files", uint32_t(fileCount));
    json.Field("reason", std::string(deskhub::TransferReasonName(client.Reason())));
    json.Field("message", client.Message());
    json.ObjectEnd();
    PrintLine(json.Text());
}

}

ExitCode RunSend(const Command& command) {
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

    std::vector<std::filesystem::path> paths;
    paths.reserve(command.send.files.size());
    for (const std::string& file : command.send.files) paths.push_back(PathOf(file));

    const deskhubp::FileBatch batch = deskhubp::InspectFiles(paths);
    if (!batch.Ok()) {
        PrintError(batch.error);
        return ExitCode::Usage;
    }

    if (!deskhubp::ProbeHostRttMs(host, command.timeoutMs)) {
        PrintError(deskhub::ui::SourceQueryFailed(command.address));
        return ExitCode::Unreachable;
    }

    std::vector<deskhub::SourceInfo> sources;
    deskhub::HostCaps caps{};
    if (!QuerySources(host, sources, passcode.value, &caps)) {
        PrintError(deskhub::ui::SourceQueryFailed(command.address));
        return ExitCode::Refused;
    }
    if (!caps.files) {
        PrintError("that host is not accepting files");
        return ExitCode::Refused;
    }

    deskhubp::FileTransferClientConfig config;
    config.host = host;
    config.hostLabel = command.address;
    config.passcode = passcode.value;
    config.clientName =
        command.deviceName ? *command.deviceName : deskhubp::SessionDeviceName();
    config.files = std::move(paths);

    deskhubp::FileTransferClientCallbacks hooks;
    if (!command.quiet && !command.json) {
        hooks.onProgress = [](const deskhub::TransferProgress& progress) {
            PrintError(deskhub::ui::TransferProgressLine(progress.name, progress.fileIndex,
                progress.fileCount, progress.batchBytes, progress.batchSize));
        };
    }

    WatchForInterrupt();

    deskhubp::FileTransferClient client;
    if (!client.Start(config, std::move(hooks))) {
        PrintError(deskhub::ui::CouldNotConnectTo(command.address));
        return ExitCode::Unreachable;
    }

    while (!Interrupted() && !client.Finished())
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));

    if (Interrupted() && !client.Finished()) client.Cancel();
    const deskhubp::FileTransferClientState last = client.State();
    const std::string message = client.Message();
    client.Stop();

    if (command.json) {
        PrintResultJson(client, command.send.files.size());
    } else if (!command.quiet) {
        PrintError(message);
    }
    if (Interrupted() && last != deskhubp::FileTransferClientState::Done)
        return ExitCode::Interrupted;
    return CodeFor(last);
}

}
