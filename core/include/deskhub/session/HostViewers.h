#pragma once
#include "deskhub/input/InputReceiver.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/ClipboardSync.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

namespace deskhub {

inline constexpr size_t kMaxViewersPerHost = 5;
inline constexpr size_t kMaxViewersPerSource = kMaxViewersPerHost;

inline constexpr uint64_t kInputControlHoldUs = 1'000'000;

class ViewerBudget {
public:
    bool TryTake();
    void Give(size_t count);

    size_t taken() const {
        return taken_.load(std::memory_order_acquire);
    }

private:
    std::atomic<size_t> taken_{0};
};

struct ViewerSlot {
    bool active = false;
    bool started = false;
    bool haveFeedback = false;
    uint32_t clientId = 0;
    uint64_t addrPacked = 0;
    uint64_t joinOrder = 0;
    uint64_t lastRecvUs = 0;
    uint64_t lastInputUs = 0;
    std::string name{};
    Feedback feedback{};
    InputReceiver input;
    ClipboardSync clip;
};

struct ViewerInfo {
    uint64_t addrPacked = 0;
    std::string name{};
};

inline bool IsDriving(const ViewerSlot& slot, uint64_t nowUs) {
    return slot.active && slot.lastInputUs && nowUs - slot.lastInputUs <= kInputControlHoldUs;
}

class ViewerTable {
public:
    ~ViewerTable();

    void SetHostBudget(ViewerBudget* budget) {
        budget_ = budget;
    }

    ViewerSlot* Find(uint64_t addrPacked);
    ViewerSlot* FindByClient(uint32_t clientId);
    ViewerSlot* Admit(uint32_t clientId, uint64_t addrPacked, uint64_t nowUs,
        std::string_view name = {});
    void Rebind(ViewerSlot& slot, uint64_t addrPacked);
    void SetName(ViewerSlot& slot, std::string_view name);
    void Drop(ViewerSlot& slot);
    void Clear();

    bool HigherPriorityIsDriving(const ViewerSlot& slot, uint64_t nowUs) const;

    bool empty() const {
        return count_ == 0;
    }
    bool anyStarted() const;

    std::span<ViewerSlot> slots() {
        return std::span<ViewerSlot>(slots_, kMaxViewersPerSource);
    }
    std::span<const ViewerSlot> slots() const {
        return std::span<const ViewerSlot>(slots_, kMaxViewersPerSource);
    }

    size_t viewerCount() const {
        return publishedCount_.load(std::memory_order_acquire);
    }
    size_t SnapshotAddrs(std::span<uint64_t> out) const;
    size_t SnapshotInfos(std::span<ViewerInfo> out) const;

    InputReceiver::Stats inputStats() const;

private:
    void Publish();

    ViewerSlot slots_[kMaxViewersPerSource];
    std::atomic<uint64_t> published_[kMaxViewersPerSource] = {};
    ViewerInfo publishedInfos_[kMaxViewersPerSource] = {};
    mutable std::mutex publishMutex_;
    std::atomic<size_t> publishedCount_{0};
    ViewerBudget* budget_ = nullptr;
    size_t count_ = 0;
    uint64_t nextJoinOrder_ = 1;
};

Feedback WorstCaseFeedback(std::span<const ViewerSlot> slots);

}
