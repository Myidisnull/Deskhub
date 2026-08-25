#pragma once
#include "deskhub/protocol/RecordStream.h"
#include "deskhub/session/host/FileReceiver.h"
#include "deskhub/session/FileTransfer.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/system/FileStore.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deskhubp {

struct FileHostCallbacks {
    std::function<void(std::string_view auditLine)> onAudit;
    std::function<void()> onTransfersChanged;
};

class FileHost {
public:
    FileHost() = default;
    ~FileHost();
    FileHost(const FileHost&) = delete;
    FileHost& operator=(const FileHost&) = delete;

    bool Start(UdpSocket& sock, const std::filesystem::path& dir, FileHostCallbacks callbacks);
    void Stop();

    bool Running() const {
        return running_.load(std::memory_order_acquire);
    }

    void SetAccepting(bool on);
    bool Accepting() const {
        return accepting_.load(std::memory_order_acquire);
    }

    std::filesystem::path Directory() const;

    void HandleDatagram(const NetAddr& from, std::span<const uint8_t> datagram);
    void OnPeerGone(const NetAddr& peer);
    void DrainGone();

    std::vector<deskhub::TransferRecord> Transfers() const;

    bool DiskKeepingUp() const {
        return diskBacklog_.load(std::memory_order_relaxed) < kMaxQueuedWriteBytes / 2;
    }

private:
    struct Peer {
        NetAddr addr{};
        std::string endpoint{};
        std::string name{};
        std::unique_ptr<FileStore> store{};
        std::unique_ptr<deskhub::FileReceiver> receiver{};
        deskhub::RecordStream stream{};
        deskhub::TransferProgress progress{};
        bool live = false;
        deskhub::TransferReason reason = deskhub::TransferReason::Accepted;
    };

    struct Outbound {
        NetAddr addr{};
        std::vector<uint8_t> bytes{};
    };

    Peer* PeerFor(const NetAddr& from);
    void RefreshLimits(Peer& peer);
    void DropPeer(const NetAddr& peer);
    void HandleMessage(const NetAddr& from, std::span<const uint8_t> message);
    void RefuseOffer(const NetAddr& from, std::span<const uint8_t> message);
    std::vector<std::string> TakeAudit();
    std::vector<Outbound> TakeOutbox();
    void FlushOutbox();

    UdpSocket* sock_ = nullptr;
    FileHostCallbacks cb_{};
    std::filesystem::path dir_{};
    std::map<uint64_t, std::unique_ptr<Peer>> peers_{};
    std::vector<std::string> audit_{};
    std::vector<Outbound> outbox_{};
    std::vector<NetAddr> gone_{};
    std::mutex goneMutex_{};
    std::mutex outboxMutex_{};
    mutable std::mutex mutex_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> accepting_{false};
    std::atomic<size_t> diskBacklog_{0};
};

}
