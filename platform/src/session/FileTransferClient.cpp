#include "deskhubp/session/FileTransferClient.h"

#include "deskhub/ui/Strings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/TrustStoreFile.h"

#include <utility>

namespace deskhubp {

namespace {

constexpr uint32_t kConnectTimeoutMs = 10'000;
constexpr uint32_t kAuthTimeoutMs = 10'000;
constexpr uint32_t kRecvTimeoutMs = 5;
constexpr uint64_t kOfferTimeoutUs = 10'000'000;

}

FileTransferClient::~FileTransferClient() {
    Stop();
}

bool FileTransferClient::Start(const FileTransferClientConfig& config,
    FileTransferClientCallbacks callbacks) {
    if (running_.load(std::memory_order_acquire)) return false;
    if (config.files.empty()) return false;

    config_ = config;
    cb_ = std::move(callbacks);
    stop_.store(false, std::memory_order_release);
    reason_.store(deskhub::TransferReason::Accepted, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    SetState(FileTransferClientState::Connecting, deskhub::ui::kTransferConnecting);

    thread_ = std::thread([this] { Loop(); });
    return true;
}

void FileTransferClient::Cancel() {
    if (upload_) upload_->Cancel();
    stop_.store(true, std::memory_order_release);
}

void FileTransferClient::Stop() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    running_.store(false, std::memory_order_release);
}

bool FileTransferClient::Finished() const {
    const FileTransferClientState state = State();
    return state == FileTransferClientState::Done ||
           state == FileTransferClientState::Refused ||
           state == FileTransferClientState::Failed ||
           state == FileTransferClientState::KeyChanged;
}

std::string FileTransferClient::Message() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return message_;
}

deskhub::TransferProgress FileTransferClient::Progress() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

deskhub::ui::TransferView FileTransferClient::View() const {
    const FileTransferClientState state = State();
    deskhub::ui::TransferView view;
    view.active = state == FileTransferClientState::Connecting ||
                  state == FileTransferClientState::Sending;
    view.done = state == FileTransferClientState::Done;
    view.failed = state == FileTransferClientState::Refused ||
                  state == FileTransferClientState::Failed ||
                  state == FileTransferClientState::KeyChanged;

    const std::lock_guard<std::mutex> lock(mutex_);
    view.fileIndex = progress_.fileIndex;
    view.fileCount = progress_.fileCount;
    view.bytes = progress_.batchBytes;
    view.total = progress_.batchSize;
    view.name = progress_.name;
    view.message = message_;
    return view;
}

void FileTransferClient::SetState(FileTransferClientState state, std::string_view message) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        message_.assign(message);
    }
    state_.store(state, std::memory_order_release);
    if (cb_.onState) cb_.onState(state, message);
}

bool FileTransferClient::Dial() {
    sock_.SetRecvTimeout(kRecvTimeoutMs);
    if (!sock_.Connect(QuicSettings{}, config_.host, config_.hostLabel)) return false;
    return sock_.WaitEstablished(config_.host, kConnectTimeoutMs);
}

bool FileTransferClient::Admit() {
    const std::string endpoint = config_.host.ToString();
    const std::optional<deskhub::Fingerprint> peer = sock_.PeerFingerprint(config_.host);
    if (!peer) {
        SetState(FileTransferClientState::Failed, deskhub::ui::kTerminalUnreachable);
        return false;
    }
    fingerprint_ = *peer;

    const deskhub::TrustVerdict verdict = CheckTrustedHost(endpoint, *peer);
    if (verdict == deskhub::TrustVerdict::Changed) {
        SetState(FileTransferClientState::KeyChanged, deskhub::ui::kTrustChangedBody);
        if (cb_.onKeyChanged) cb_.onKeyChanged(FormatFingerprint(*peer));
        return false;
    }

    ClientAuthConfig auth;
    auth.identity = LoadOrCreateHostIdentity(config_.clientName);
    auth.passcode = config_.passcode;
    auth.hostFingerprint = *peer;
    auth.clientName = config_.clientName;

    deskhub::AuthResultCode code = deskhub::AuthResultCode::NotPaired;
    bool hostProved = false;
    if (!sock_.RunClientAuth(config_.host, std::move(auth), kAuthTimeoutMs, code, hostProved)) {
        SetState(FileTransferClientState::Refused, deskhub::ui::AuthRefusalText(code));
        return false;
    }
    if (verdict == deskhub::TrustVerdict::Unknown) RememberIfPasscodeProvedIt(hostProved);
    return true;
}

void FileTransferClient::RememberIfPasscodeProvedIt(bool hostProved) {
    if (!hostProved || deskhub::IsZero(fingerprint_)) return;
    RememberTrustedHost(config_.host.ToString(), config_.hostLabel, fingerprint_,
        NowUnixSeconds());
    LOGI("transfer: passcode accepted \xE2\x80\x94 remembering %s as %s",
        config_.host.ToString().c_str(), FormatFingerprint(fingerprint_).c_str());
}

void FileTransferClient::Loop() {
    if (!Dial()) {
        SetState(FileTransferClientState::Failed, deskhub::ui::kTerminalUnreachable);
        sock_.Close();
        running_.store(false, std::memory_order_release);
        return;
    }
    if (!Admit()) {
        sock_.Close();
        running_.store(false, std::memory_order_release);
        return;
    }

    FileUploadCallbacks hooks;
    hooks.send = [this](std::span<const uint8_t> message) {
        return sock_.SendRecordOn(config_.host, kQuicFileStream, message);
    };
    hooks.onProgress = [this](const deskhub::TransferProgress& progress) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            progress_ = progress;
        }
        if (cb_.onProgress) cb_.onProgress(progress);
    };
    upload_ = std::make_unique<FileUpload>(std::move(hooks));

    if (!upload_->Begin(config_.files)) {
        reason_.store(deskhub::TransferReason::ReadFailed, std::memory_order_release);
        SetState(FileTransferClientState::Failed, upload_->LastError());
        sock_.Close();
        running_.store(false, std::memory_order_release);
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        progress_ = upload_->Progress();
    }
    SetState(FileTransferClientState::Sending, deskhub::ui::kTransferSending);

    const uint64_t offerDeadlineUs = NowUs() + kOfferTimeoutUs;
    bool unanswered = false;
    uint8_t buf[deskhub::kMaxRecordSize];
    while (!stop_.load(std::memory_order_acquire)) {
        NetAddr from;
        const int n = sock_.RecvFrom(buf, sizeof(buf), from);
        if (n < 0) {
            upload_->LinkLost();
            break;
        }
        if (n > 0) {
            const std::span<const uint8_t> message(buf, size_t(n));
            const auto header = deskhub::ParseCommonHeader(message);
            if (header && header->chan == deskhub::Chan::File) upload_->HandleMessage(message);
        }
        upload_->Pump();
        if (!upload_->Busy()) break;
        if (upload_->State() == deskhub::FileSenderState::Offering && NowUs() > offerDeadlineUs) {
            unanswered = true;
            upload_->Cancel();
            break;
        }
    }
    if (upload_->Busy()) upload_->LinkLost();

    if (unanswered) {
        reason_.store(deskhub::TransferReason::NotAccepting, std::memory_order_release);
        SetState(FileTransferClientState::Refused, deskhub::ui::kTransferHostNotTaking);
        sock_.Close();
        running_.store(false, std::memory_order_release);
        return;
    }

    const deskhub::FileSenderState last = upload_->State();
    const deskhub::TransferReason reason = upload_->Reason();
    reason_.store(reason, std::memory_order_release);

    if (last == deskhub::FileSenderState::Done)
        SetState(FileTransferClientState::Done, deskhub::ui::kTransferDone);
    else if (last == deskhub::FileSenderState::Refused)
        SetState(FileTransferClientState::Refused, deskhub::ui::TransferReasonText(reason));
    else
        SetState(FileTransferClientState::Failed, deskhub::ui::TransferReasonText(reason));

    sock_.Close();
    running_.store(false, std::memory_order_release);
}

}
