#pragma once
#include "deskhub/session/FileSender.h"
#include "deskhub/session/FileTransfer.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace deskhubp {

inline constexpr size_t kFileChunksPerTick = 32;

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

    bool Begin(const std::vector<std::filesystem::path>& paths);
    void HandleMessage(std::span<const uint8_t> message);
    size_t Pump(size_t maxChunks = kFileChunksPerTick);
    void Cancel();
    void LinkLost();

    bool Busy() const;
    deskhub::FileSenderState State() const;
    deskhub::TransferReason Reason() const;
    deskhub::TransferProgress Progress() const;
    std::string LastError() const;

private:
    struct Pending {
        bool haveProgress = false;
        deskhub::TransferProgress progress{};
        bool haveFinish = false;
        deskhub::FileSenderState state = deskhub::FileSenderState::Idle;
        deskhub::TransferReason reason = deskhub::TransferReason::Accepted;
    };

    size_t ReadAt(uint16_t index, uint64_t offset, std::span<uint8_t> out);
    void Flush();

    mutable std::mutex mutex_{};
    FileUploadCallbacks cb_{};
    std::unique_ptr<deskhub::FileSender> sender_{};
    std::vector<std::ifstream> sources_{};
    std::string error_{};
    Pending pending_{};
    uint32_t nextBatchId_ = 1;
};

}
