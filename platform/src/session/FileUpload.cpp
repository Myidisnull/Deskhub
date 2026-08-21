#include "deskhubp/session/FileUpload.h"

#include "deskhub/transfer/SafeName.h"
#include "deskhubp/diag/Log.h"

#include <algorithm>
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
        readerStop_ = true;
        pending_.haveFinish = true;
        pending_.state = state;
        pending_.reason = reason;
    };
    sender_ = std::make_unique<deskhub::FileSender>(std::move(hooks));
}

FileUpload::~FileUpload() {
    StopReader();
}

void FileUpload::StartReader() {
    ring_.clear();
    ringBytes_ = 0;
    readerStop_ = false;
    readerEof_ = false;
    readerFailed_ = false;
    reader_ = std::thread([this] { ReaderThread(); });
}

void FileUpload::StopReader() {
    if (!reader_.joinable()) return;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        readerStop_ = true;
    }
    roomCv_.notify_all();
    reader_.join();
}

void FileUpload::ReaderThread() {
    for (size_t at = 0; at < plan_.size(); ++at) {
        const uint16_t index = uint16_t(at);
        const uint64_t size = plan_[at].size;
        uint64_t offset = 0;
        while (offset < size) {
            const uint64_t left = size - offset;
            const size_t want =
                size_t(left < deskhub::kMaxFileChunkBytes ? left : deskhub::kMaxFileChunkBytes);

            ReadChunk chunk;
            chunk.index = index;
            chunk.offset = offset;
            chunk.data.resize(want);

            std::ifstream& in = sources_[index];
            in.clear();
            in.seekg(std::streamoff(offset), std::ios::beg);
            if (!in) {
                const std::lock_guard<std::mutex> lock(mutex_);
                readerFailed_ = true;
                readyCv_.notify_all();
                return;
            }
            in.read(reinterpret_cast<char*>(chunk.data.data()), std::streamsize(want));
            const std::streamsize got = in.gcount();
            if (got <= 0) {
                const std::lock_guard<std::mutex> lock(mutex_);
                readerFailed_ = true;
                readyCv_.notify_all();
                return;
            }
            chunk.data.resize(size_t(got));

            {
                std::unique_lock<std::mutex> lock(mutex_);
                roomCv_.wait(lock, [this] {
                    return readerStop_ || ringBytes_ < kMaxReadAheadBytes;
                });
                if (readerStop_) return;
                ringBytes_ += chunk.data.size();
                ring_.push_back(std::move(chunk));
            }
            readyCv_.notify_all();
            offset += uint64_t(got);
        }
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    readerEof_ = true;
    readyCv_.notify_all();
}

size_t FileUpload::Pumpable() const {
    if (readerFailed_) return 1;

    const deskhub::TransferProgress at = sender_->Progress();
    size_t ready = 0;
    for (const ReadChunk& chunk : ring_)
        if (chunk.index > at.fileIndex ||
            (chunk.index == at.fileIndex && chunk.offset >= at.fileBytes))
            ++ready;

    if (ready > 0) return ready;
    return readerEof_ ? 1 : 0;
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
    StopReader();
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
    plan_ = batch.files;
    if (!sender_->Offer(nextBatchId_, std::move(batch.files))) {
        sources_.clear();
        plan_.clear();
        error_ = "The batch is more than one transfer can carry.";
        return false;
    }
    ++nextBatchId_;
    StartReader();
    return true;
}

size_t FileUpload::ReadAt(uint16_t index, uint64_t offset, std::span<uint8_t> out) {
    while (!ring_.empty()) {
        const ReadChunk& head = ring_.front();
        if (head.index > index || (head.index == index && head.offset >= offset)) break;
        ringBytes_ -= head.data.size();
        ring_.pop_front();
        roomCv_.notify_all();
    }
    if (ring_.empty()) return 0;

    const ReadChunk& head = ring_.front();
    if (head.index != index || head.offset != offset) return 0;
    const size_t n = std::min(out.size(), head.data.size());
    std::copy_n(head.data.begin(), n, out.begin());
    return n;
}

void FileUpload::Flush() {
    Pending ready;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ready = pending_;
        pending_ = Pending{};
    }
    if (ready.haveFinish) {
        StopReader();
        const std::lock_guard<std::mutex> lock(mutex_);
        sources_.clear();
        plan_.clear();
        ring_.clear();
        ringBytes_ = 0;
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
        if (sender_->State() == deskhub::FileSenderState::Sending) {
            const size_t budget = std::min(maxChunks, Pumpable());
            if (budget > 0) sent = sender_->Pump(budget);
        }
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

deskhub::ui::TransferView FileUpload::View() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return deskhub::ui::TransferViewOf(sender_->State(), sender_->Reason(), sender_->Progress());
}

std::string FileUpload::LastError() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

}
