#include "deskhubp/client/FileTransferClient.h"

#include "deskhub/ui/Strings.h"
#include "deskhubp/net/FileUdp.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/MachineId.h"

#include <utility>

namespace deskhubp {

namespace {

constexpr uint32_t kRecvWaitMs = 20;
constexpr uint64_t kOfferTimeoutUs = 10'000'000;
constexpr uint64_t kPairingLegacyUs = 400'000;
constexpr uint64_t kPairingWaitUs = 8'000'000;
constexpr uint64_t kPairingRetryUs = 1'000'000;

}

FileTransferClient::~FileTransferClient() {
    Stop();
}

bool FileTransferClient::Start(const FileTransferClientConfig& config,
    FileTransferClientCallbacks callbacks) {
    if (running_.load(std::memory_order_acquire)) return false;
    if (thread_.joinable()) thread_.join();
    if (config.files.empty()) return false;

    config_ = config;
    cb_ = std::move(callbacks);
    stop_.store(false, std::memory_order_release);
    reason_.store(deskhub::TransferReason::Accepted, std::memory_order_release);
    stream_ = deskhub::RecordStream{};
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        progress_ = deskhub::TransferProgress{};
    }

    if (!sock_.Open(0)) {
        SetState(FileTransferClientState::Failed, deskhub::ui::kTerminalUnreachable);
        return false;
    }
    sock_.SetRecvTimeout(kRecvWaitMs);

    FileUploadCallbacks uploadHooks;
    uploadHooks.send = [this](std::span<const uint8_t> message) {
        return SendFileMessage(sock_, config_.host, message);
    };
    uploadHooks.onProgress = [this](const deskhub::TransferProgress& progress) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            progress_ = progress;
        }
        if (cb_.onProgress) cb_.onProgress(progress);
    };
    upload_ = std::make_unique<FileUpload>(std::move(uploadHooks));

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
    sock_.Close();
    upload_.reset();
    running_.store(false, std::memory_order_release);
}

bool FileTransferClient::Finished() const {
    const FileTransferClientState state = State();
    return state == FileTransferClientState::Done ||
           state == FileTransferClientState::Refused ||
           state == FileTransferClientState::Failed;
}

std::string FileTransferClient::Message() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return message_;
}

deskhub::TransferProgress FileTransferClient::Progress() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

void FileTransferClient::SetState(FileTransferClientState state, std::string_view message) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        message_.assign(message);
    }
    state_.store(state, std::memory_order_release);
    if (cb_.onState) cb_.onState(state, message);
}

bool FileTransferClient::EnsurePaired() {
    deskhub::PairingHello hello;
    hello.fingerprint = LoadOrCreateMachineFingerprint();
    hello.clientName = config_.clientName;

    uint8_t buf[deskhub::kMaxDatagram];
    const size_t helloBytes = deskhub::BuildPairingHello(buf, hello);
    if (!helloBytes) return true;

    const uint64_t started = NowUs();
    uint64_t nextHello = started;
    bool sawHost = false;
    uint8_t rx[deskhub::kMaxDatagram];

    while (NowUs() - started < kPairingWaitUs) {
        if (stop_.load(std::memory_order_acquire)) return false;
        if (NowUs() >= nextHello) {
            if (!sock_.SendTo(config_.host, buf, helloBytes)) return true;
            nextHello = NowUs() + kPairingRetryUs;
        }

        NetAddr from;
        const int n = sock_.RecvFrom(rx, sizeof(rx), from);
        if (n <= 0) {
            if (!sawHost && NowUs() - started >= kPairingLegacyUs) return true;
            continue;
        }
        if (from != config_.host) continue;

        const std::optional<deskhub::CommonHeader> header =
            deskhub::ParseCommonHeader(std::span<const uint8_t>(rx, size_t(n)));
        if (!header || header->chan != deskhub::Chan::Control) continue;
        if (header->type == deskhub::MsgType::PairingResult) {
            sawHost = true;
            const std::optional<deskhub::PairingResult> result =
                deskhub::ParsePairingResult(
                    deskhub::PayloadOf(std::span<const uint8_t>(rx, size_t(n))));
            if (!result) continue;
            if (result->code == deskhub::PairingResultCode::Accepted) return true;
            if (result->code == deskhub::PairingResultCode::Disabled ||
                result->code == deskhub::PairingResultCode::Refused)
                return false;
        }
    }

    return !sawHost;
}

void FileTransferClient::Loop() {
    if (!EnsurePaired()) {
        SetState(FileTransferClientState::Refused, deskhub::ui::kTerminalClosed);
        running_.store(false, std::memory_order_release);
        return;
    }
    if (!stop_.load(std::memory_order_acquire)) ServeUpload();
    running_.store(false, std::memory_order_release);
}

void FileTransferClient::ServeUpload() {
    if (!upload_->Begin(config_.files)) {
        reason_.store(deskhub::TransferReason::ReadFailed, std::memory_order_release);
        SetState(FileTransferClientState::Failed, upload_->LastError());
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        progress_ = upload_->Progress();
    }
    SetState(FileTransferClientState::Sending, deskhub::ui::kTransferSending);

    const uint64_t offerDeadlineUs = NowUs() + kOfferTimeoutUs;
    bool unanswered = false;
    uint8_t buf[deskhub::kMaxDatagram];

    while (!stop_.load(std::memory_order_acquire)) {
        NetAddr from;
        const int n = sock_.RecvFrom(buf, sizeof(buf), from);
        if (n > 0 && from == config_.host) {
            std::vector<std::vector<uint8_t>> messages;
            FeedFileDatagram(stream_, std::span<const uint8_t>(buf, size_t(n)), messages);
            for (const std::vector<uint8_t>& message : messages) upload_->HandleMessage(message);
        } else if (n < 0) {
            upload_->LinkLost();
            break;
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
}

}
