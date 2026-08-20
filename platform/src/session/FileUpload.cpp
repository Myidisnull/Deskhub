#include "deskhubp/session/FileUpload.h"

#include "deskhub/transfer/SafeName.h"
#include "deskhubp/diag/Log.h"

#include <system_error>
#include <utility>

namespace deskhubp {

namespace {

std::string Utf8Of(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

}

FileUpload::FileUpload(FileUploadCallbacks callbacks) : cb_(std::move(callbacks)) {
    deskhub::FileSenderCallbacks hooks;
    hooks.send = [this](std::span<const uint8_t> message) {
        return cb_.send && cb_.send(message);
    };
    hooks.read = [this](uint16_t index, uint64_t offset, std::span<uint8_t> out) {
        return ReadAt(index, offset, out);
    };
    hooks.onProgress = [this](const deskhub::TransferProgress& progress) {
        pending_.haveProgress = true;
        pending_.progress = progress;
    };
    hooks.onFinished = [this](deskhub::FileSenderState state, deskhub::TransferReason reason) {
        sources_.clear();
        pending_.haveFinish = true;
        pending_.state = state;
        pending_.reason = reason;
    };
    sender_ = std::make_unique<deskhub::FileSender>(std::move(hooks));
}

FileBatch InspectFiles(const std::vector<std::filesystem::path>& paths) {
    FileBatch batch;
    if (paths.empty()) {
        batch.error = "No file chosen.";
        return batch;
    }
    if (paths.size() > deskhub::kMaxTransferFiles) {
        batch.error = "Too many files at once — " +
                      std::to_string(deskhub::kMaxTransferFiles) +
                      " is the most one batch carries.";
        return batch;
    }

    batch.files.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec))
            return FileBatch{{}, Utf8Of(path) + " is not a file that can be sent."};
        const uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec) return FileBatch{{}, Utf8Of(path) + " could not be measured."};
        const std::string name = deskhub::SafeFileName(Utf8Of(path.filename()));
        if (name.empty()) return FileBatch{{}, Utf8Of(path) + " has no name that can be stored."};
        batch.files.push_back(deskhub::TransferFile{uint64_t(size), name});
    }
    return batch;
}

bool FileUpload::Begin(const std::vector<std::filesystem::path>& paths) {
    const std::lock_guard<std::mutex> lock(mutex_);
    error_.clear();

    if (sender_->Busy()) {
        error_ = "A transfer is already running.";
        return false;
    }

    FileBatch batch = InspectFiles(paths);
    if (!batch.Ok()) {
        error_ = batch.error;
        return false;
    }

    std::vector<std::ifstream> opened;
    opened.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            error_ = Utf8Of(path) + " could not be opened for reading.";
            return false;
        }
        opened.push_back(std::move(in));
    }

    sources_ = std::move(opened);
    if (!sender_->Offer(nextBatchId_, std::move(batch.files))) {
        sources_.clear();
        error_ = "The batch is more than one transfer can carry.";
        return false;
    }
    ++nextBatchId_;
    return true;
}

size_t FileUpload::ReadAt(uint16_t index, uint64_t offset, std::span<uint8_t> out) {
    if (index >= sources_.size()) return 0;
    std::ifstream& in = sources_[index];
    in.clear();
    in.seekg(std::streamoff(offset), std::ios::beg);
    if (!in) return 0;
    in.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
    const std::streamsize got = in.gcount();
    return got > 0 ? size_t(got) : 0;
}

void FileUpload::Flush() {
    Pending ready;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ready = pending_;
        pending_ = Pending{};
    }
    if (ready.haveProgress && cb_.onProgress) cb_.onProgress(ready.progress);
    if (ready.haveFinish && cb_.onFinished) cb_.onFinished(ready.state, ready.reason);
}

void FileUpload::HandleMessage(std::span<const uint8_t> message) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        sender_->HandleMessage(message);
    }
    Flush();
}

size_t FileUpload::Pump(size_t maxChunks) {
    size_t sent = 0;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (sender_->State() == deskhub::FileSenderState::Sending)
            sent = sender_->Pump(maxChunks);
    }
    Flush();
    return sent;
}

void FileUpload::Cancel() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        sender_->Cancel();
    }
    Flush();
}

void FileUpload::LinkLost() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        sender_->LinkLost();
    }
    Flush();
}

bool FileUpload::Busy() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return sender_->Busy();
}

deskhub::FileSenderState FileUpload::State() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return sender_->State();
}

deskhub::TransferReason FileUpload::Reason() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return sender_->Reason();
}

deskhub::TransferProgress FileUpload::Progress() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return sender_->Progress();
}

std::string FileUpload::LastError() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

}
