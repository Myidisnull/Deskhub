#include "deskhubp/system/FileStore.h"

#include "deskhub/transfer/SafeName.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Environment.h"

#include <system_error>
#include <utility>

namespace deskhubp {

namespace {

std::filesystem::path HomePath() {
#ifdef _WIN32
    const std::string home = EnvValue("USERPROFILE");
#else
    const std::string home = EnvValue("HOME");
#endif
    if (home.empty()) return {};
    const std::u8string wide(home.begin(), home.end());
    return std::filesystem::path(wide);
}

std::filesystem::path Utf8Path(const std::string& text) {
    const std::u8string wide(text.begin(), text.end());
    return std::filesystem::path(wide);
}

std::string Utf8Of(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

}

std::filesystem::path DefaultTransferDir() {
    const std::filesystem::path home = HomePath();
    if (home.empty()) return {};
    return home / kTransferDirName;
}

FileStore::~FileStore() {
    CloseAll();
}

bool FileStore::SetDirectory(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::is_directory(dir, ec)) {
        LOGE("transfer: %s is not a directory files can be stored in", Utf8Of(dir).c_str());
        return false;
    }
    dir_ = dir;
    return true;
}

uint64_t FileStore::FreeBytes() const {
    if (dir_.empty()) return 0;
    std::error_code ec;
    const std::filesystem::space_info space = std::filesystem::space(dir_, ec);
    if (ec) return 0;
    return uint64_t(space.available);
}

bool FileStore::Claimed(const std::string& name) const {
    std::error_code ec;
    if (std::filesystem::exists(dir_ / Utf8Path(name), ec)) return true;
    for (const auto& [index, slot] : open_)
        if (slot.name == name) return true;
    return false;
}

std::string FileStore::Open(uint16_t index, const std::string& safeName, uint64_t size) {
    if (dir_.empty()) return {};
    Close(index, false);

    std::string name = deskhub::UniqueFileName(safeName,
        [this](const std::string& candidate) { return Claimed(candidate); });
    if (name.empty()) {
        LOGW("transfer: no free name left beside %s", safeName.c_str());
        return {};
    }

    if (size > FreeBytes()) {
        LOGW("transfer: %s needs more room than %s has left", name.c_str(),
            Utf8Of(dir_).c_str());
        return {};
    }

    Slot slot;
    slot.name = name;
    slot.target = dir_ / Utf8Path(name);
    slot.part = dir_ / Utf8Path(name + kTransferPartSuffix);
    slot.out.open(slot.part, std::ios::binary | std::ios::trunc);
    if (!slot.out) {
        LOGE("transfer: could not open %s for writing", Utf8Of(slot.part).c_str());
        return {};
    }

    open_.emplace(index, std::move(slot));
    return name;
}

bool FileStore::Write(uint16_t index, std::span<const uint8_t> data) {
    const auto at = open_.find(index);
    if (at == open_.end()) return false;
    at->second.out.write(reinterpret_cast<const char*>(data.data()),
        std::streamsize(data.size()));
    return bool(at->second.out);
}

void FileStore::Close(uint16_t index, bool keep) {
    const auto at = open_.find(index);
    if (at == open_.end()) return;
    Slot slot = std::move(at->second);
    open_.erase(at);

    slot.out.flush();
    const bool sound = bool(slot.out);
    slot.out.close();

    std::error_code ec;
    if (!keep || !sound) {
        std::filesystem::remove(slot.part, ec);
        return;
    }

    std::filesystem::path target = slot.target;
    if (std::filesystem::exists(target, ec)) {
        const std::string fresh = deskhub::UniqueFileName(slot.name,
            [this](const std::string& candidate) { return Claimed(candidate); });
        if (fresh.empty()) {
            std::filesystem::remove(slot.part, ec);
            return;
        }
        target = dir_ / Utf8Path(fresh);
    }

    std::filesystem::rename(slot.part, target, ec);
    if (ec) {
        LOGE("transfer: could not put %s in place", Utf8Of(target).c_str());
        std::filesystem::remove(slot.part, ec);
    }
}

void FileStore::CloseAll() {
    while (!open_.empty()) Close(open_.begin()->first, false);
}

}
