#pragma once
#include "deskhub/session/client/FileSender.h"
#include "deskhub/session/FileTransfer.h"
#include "deskhub/ui/TransferView.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace deskhubp {

inline constexpr size_t kFileChunksPerTick = 8;
inline constexpr size_t kMaxReadAheadBytes = 1u << 20;

struct FileBatch {
    std::vector<deskhub::TransferFile> files{};
    std::string error{};

    bool Ok() const {
        return error.empty();
    }
};

FileBatch InspectFiles(const std::vector<std::filesystem::path>& paths);

struct FileUploadCallbacks {
    std::function<bool(std::span<const uint8_t> message)> send;
    std::function<void(const deskhub::TransferProgress&)> onProgress;
    std::function<void(deskhub::FileSenderState, deskhub::TransferReason)> onFinished;
};

class FileUpload {
public:
    explicit FileUpload(FileUploadCallbacks callbacks);
    ~FileUpload();
    FileUpload(const FileUpload&) = delete;
    FileUpload& operator=(const FileUpload&) = delete;

    bool Begin(const std::vector<std::filesystem::path>& paths);
    void HandleMessage(std::span<const uint8_t> message);
    size_t Pump(size_t maxChunks = kFileChunksPerTick);
    void Cancel();
    void LinkLost();

    bool Busy() const;
    deskhub::FileSenderState State() const;
    deskhub::TransferReason Reason() const;
    deskhub::TransferProgress Progress() const;
    deskhub::ui::TransferView View() const;
    std::string LastError() const;

private:
    struct Pending {
        bool haveProgress = false;
        deskhub::TransferProgress progress{};
        bool haveFinish = false;
        deskhub::FileSenderState state = deskhub::FileSenderState::Idle;
        deskhub::TransferReason reason = deskhub::TransferReason::Accepted;
    };

    struct ReadChunk {
        uint16_t index = 0;
        uint64_t offset = 0;
        std::vector<uint8_t> data{};
    };

    size_t ReadAt(uint16_t index, uint64_t offset, std::span<uint8_t> out);
    void Flush();
    void StartReader();
    void StopReader();
    void ReaderThread();
    size_t Pumpable() const;

    mutable std::mutex mutex_{};
    FileUploadCallbacks cb_{};
    std::unique_ptr<deskhub::FileSender> sender_{};
    std::vector<std::ifstream> sources_{};
    std::string error_{};
    Pending pending_{};
    uint32_t nextBatchId_ = 1;

    std::thread reader_{};
    std::condition_variable roomCv_{};
    std::condition_variable readyCv_{};
    std::deque<ReadChunk> ring_{};
    std::vector<deskhub::TransferFile> plan_{};
    size_t ringBytes_ = 0;
    bool readerStop_ = false;
    bool readerEof_ = false;
    bool readerFailed_ = false;
};

}
