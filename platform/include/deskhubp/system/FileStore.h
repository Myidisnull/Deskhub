#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>

namespace deskhubp {

inline constexpr const char* kTransferDirName = "Deskhub";
inline constexpr const char* kTransferPartSuffix = ".deskhub-part";

std::filesystem::path DefaultTransferDir();

class FileStore {
public:
    FileStore() = default;
    ~FileStore();
    FileStore(const FileStore&) = delete;
    FileStore& operator=(const FileStore&) = delete;

    bool SetDirectory(const std::filesystem::path& dir);

    const std::filesystem::path& Directory() const {
        return dir_;
    }

    uint64_t FreeBytes() const;

    std::string Open(uint16_t index, const std::string& safeName, uint64_t size);
    bool Write(uint16_t index, std::span<const uint8_t> data);
    void Close(uint16_t index, bool keep);
    void CloseAll();

private:
    struct Slot {
        std::string name{};
        std::filesystem::path part{};
        std::filesystem::path target{};
        std::ofstream out{};
    };

    bool Claimed(const std::string& name) const;

    std::filesystem::path dir_{};
    std::map<uint16_t, Slot> open_{};
};

}
