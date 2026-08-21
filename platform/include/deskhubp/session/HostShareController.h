#pragma once
#include "deskhub/net/PairedDevices.h"
#include "deskhub/session/FileTransfer.h"
#include "deskhub/session/TerminalSession.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/session/AgentLoop.h"
#include "deskhubp/session/FileHost.h"
#include "deskhubp/session/TerminalHost.h"
#include "deskhubp/system/FileStore.h"
#include "deskhubp/system/PairedDevicesFile.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace deskhubp {

struct HostShareBanner {
    bool screenSharing = false;
    bool hosting = false;
    uint16_t port = 0;
    bool viewOnly = false;
    std::string passcodeNote;
    std::string bindWarning;
};

class HostShareController {
public:
    struct Hooks {
        std::function<void(const std::string& message)> onError;
        std::function<void(std::function<void()>)> postToUi;
        std::function<bool(uint32_t termId)> openLocalTerminal;
        std::function<void()> onRowsChanged;
        std::function<bool(const PairingRequest& request)> askPairing;
        std::function<void()> onBannerChanged;
        std::function<void()> onNothingLeftShared;
    };

    void SetHooks(Hooks hooks) {
        hooks_ = std::move(hooks);
    }

    AgentLoop& agentLoop() {
        return agentLoop_;
    }

    TerminalHost& terminalHost() {
        return terminalHost_;
    }

    const TerminalHost& terminalHost() const {
        return terminalHost_;
    }

    FileHost& fileHost() {
        return fileHost_;
    }

    const FileHost& fileHost() const {
        return fileHost_;
    }

    const std::vector<deskhub::TerminalRecord>& shells() const {
        return shells_;
    }

    const std::vector<deskhub::TransferRecord>& transfers() const {
        return transfers_;
    }

    void StartTerminalShare() {
        if (terminalHost_.Running()) return;

        TerminalHostCallbacks callbacks;
        callbacks.onSessionsChanged = [this] {
            hooks_.postToUi([this] {
                RefreshShells();
                hooks_.onRowsChanged();
            });
        };

        if (!terminalHost_.Start(agentLoop_.Socket(), std::string(), std::move(callbacks))) {
            hooks_.onError(deskhub::ui::kShareStartFailed);
            return;
        }
        RefreshShells();
    }

    void StopTerminalShare() {
        if (!terminalHost_.Running()) return;
        terminalHost_.Stop();
        shells_.clear();
    }

    bool StartFileShare(const std::filesystem::path& folder) {
        if (fileHost_.Running()) return true;

        if (!fileHost_.Start(agentLoop_.Socket(), folder, FileHostCallbacks{})) {
            hooks_.onError(deskhub::ui::TransferFolderUnusable(PathText(folder)));
            return false;
        }
        RefreshTransfers();
        return true;
    }

    void StopFileShare() {
        if (!fileHost_.Running()) return;
        fileHost_.Stop();
        transfers_.clear();
    }

    void StopTerminalRow(bool screenSharing) {
        StopTerminalShare();
        FinishRowRemoval(screenSharing || fileHost_.Running());
    }

    void StopFilesRow(bool screenSharing) {
        StopFileShare();
        FinishRowRemoval(screenSharing || terminalHost_.Running());
    }

    void RefreshShells() {
        shells_ = terminalHost_.Running() ? terminalHost_.Sessions()
                                          : std::vector<deskhub::TerminalRecord>{};
    }

    void RefreshTransfers() {
        transfers_ = fileHost_.Running() ? fileHost_.Transfers()
                                         : std::vector<deskhub::TransferRecord>{};
    }

    void KickShell(uint32_t termId) {
        if (!terminalHost_.Running()) return;
        terminalHost_.KickSession(termId);
    }

    void StopAndAttachShell(uint32_t termId) {
        if (!terminalHost_.AttachLocal(termId)) return;
        RefreshShells();
        hooks_.onRowsChanged();
        if (!hooks_.openLocalTerminal(termId)) KickShell(termId);
    }

    void DrainPairingRequests() {
        if (askingPairing_) return;
        const std::vector<PairingRequest> requests = agentLoop_.TakePairingRequests();
        if (requests.empty()) return;

        askingPairing_ = true;
        std::map<std::string, bool> answers;
        const deskhub::PairedDevices paired = LoadPairedDevices();
        for (const deskhub::PairedDevice& device : paired.Devices())
            answers[deskhub::ShortFingerprint(device.fingerprint)] = true;

        const auto answerFor = [this, &answers](const PairingRequest& request) {
            const auto found = answers.find(request.shortKey);
            if (found != answers.end()) return found->second;
            const bool allowed = hooks_.askPairing(request);
            answers[request.shortKey] = allowed;
            return allowed;
        };
        for (const PairingRequest& request : requests)
            agentLoop_.AnswerPairing(request.addrPacked, answerFor(request));
        askingPairing_ = false;
    }

    std::string BannerText(const HostShareBanner& banner) const {
        std::string status = deskhub::ui::ShareSummaryLine(banner.screenSharing,
            terminalHost_.Running(), fileHost_.Running(), banner.port);
        if (fileHost_.Running())
            status += "\n" + deskhub::ui::TransferFolderNote(PathText(fileHost_.Directory()));
        if (!banner.passcodeNote.empty()) status += "\n" + banner.passcodeNote;
        if (banner.screenSharing && banner.viewOnly)
            status += std::string("\n") + deskhub::ui::kViewOnlyNote;
        if (banner.hosting && !banner.bindWarning.empty()) status += "\n" + banner.bindWarning;
        return status;
    }

private:
    void FinishRowRemoval(bool anythingLeft) {
        if (!anythingLeft) {
            hooks_.onNothingLeftShared();
            return;
        }
        hooks_.onBannerChanged();
        hooks_.onRowsChanged();
    }

    Hooks hooks_;
    AgentLoop agentLoop_;
    TerminalHost terminalHost_;
    FileHost fileHost_;
    std::vector<deskhub::TerminalRecord> shells_;
    std::vector<deskhub::TransferRecord> transfers_;
    bool askingPairing_ = false;
};

}
