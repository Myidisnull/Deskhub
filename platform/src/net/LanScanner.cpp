#include "deskhubp/net/LanScanner.h"

#include <algorithm>
#include <set>
#include <thread>
#include <utility>

#include "deskhub/net/LanScan.h"
#include "deskhubp/net/HostProbe.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"

namespace deskhubp {

namespace {

constexpr size_t kBatchSize = 128;
constexpr uint32_t kBatchTimeoutMs = 900;

}

std::vector<uint32_t> LocalScanTargets() {
    const std::vector<AdapterAddr> adapters = ListLocalIPv4();

    std::set<uint32_t> claimed;
    for (const AdapterAddr& adapter : adapters) {
        NetAddr self{};
        if (ParseNetAddr(adapter.ip, self)) claimed.insert(self.ip);
    }

    std::vector<uint32_t> targets;
    for (const AdapterAddr& adapter : adapters) {
        if (targets.size() >= deskhub::kMaxScanTargets) break;
        if (adapter.virtualAdapter) continue;
        NetAddr self{};
        if (!ParseNetAddr(adapter.ip, self)) continue;

        const size_t budget = deskhub::kMaxScanTargets - targets.size();
        for (uint32_t ip : deskhub::SubnetScanTargets(self.ip, adapter.prefixLen, budget))
            if (claimed.insert(ip).second) targets.push_back(ip);
    }
    return targets;
}

LanScanner::~LanScanner() {
    Cancel();
}

bool LanScanner::Busy() const {
    return state_->busy.load(std::memory_order_acquire);
}

void LanScanner::Cancel() {
    state_->cancelled.store(true, std::memory_order_release);
}

bool LanScanner::Start(uint16_t port, UiPost postToUi, HitHandler onHit,
    ProgressHandler onProgress, DoneHandler onDone) {
    if (state_->busy.exchange(true, std::memory_order_acq_rel)) return false;
    state_->cancelled.store(false, std::memory_order_release);

    std::thread([state = state_, port, postToUi = std::move(postToUi), onHit = std::move(onHit),
                    onProgress = std::move(onProgress), onDone = std::move(onDone)] {
        const auto stopped = [&state] { return state->cancelled.load(std::memory_order_acquire); };
        const auto post = [&postToUi, &state](std::function<void()> fn) {
            postToUi([state, fn = std::move(fn)] {
                if (!state->cancelled.load(std::memory_order_acquire)) fn();
            });
        };

        const std::vector<uint32_t> targets = LocalScanTargets();
        ScanProgress progress{0, targets.size(), 0};

        for (size_t begin = 0; begin < targets.size() && !stopped(); begin += kBatchSize) {
            const size_t end = std::min(begin + kBatchSize, targets.size());

            std::vector<NetAddr> batch;
            batch.reserve(end - begin);
            for (size_t i = begin; i < end; ++i) batch.push_back(NetAddr{targets[i], port});

            const auto rtts = ProbeHostsRttMs(batch, kBatchTimeoutMs);
            if (stopped()) break;

            for (size_t i = 0; i < rtts.size(); ++i) {
                if (!rtts[i]) continue;
                ++progress.found;
                ScanHit hit{deskhub::ScanAddressText(batch[i].ip, port), *rtts[i]};
                post([onHit, hit = std::move(hit)] { onHit(hit); });
            }

            progress.probed = end;
            post([onProgress, progress] { onProgress(progress); });
        }

        const bool cancelled = stopped();
        state->busy.store(false, std::memory_order_release);
        if (!cancelled) post([onDone, progress] { onDone(progress); });
    }).detach();

    return true;
}

}
