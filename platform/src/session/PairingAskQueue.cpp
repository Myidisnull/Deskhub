#include "deskhubp/session/PairingAskQueue.h"

#include <algorithm>

namespace deskhubp {

void PairingAskQueue::Push(PairingAsk ask) {
    if (ask.addrPacked == 0) return;
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const PairingAsk& existing : asks_)
        if (existing.addrPacked == ask.addrPacked) return;
    if (asks_.size() >= kMaxQueued) asks_.erase(asks_.begin());
    asks_.push_back(std::move(ask));
}

std::vector<PairingAsk> PairingAskQueue::Take(size_t maxCount) {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PairingAsk> out;
    if (maxCount >= asks_.size()) {
        out.swap(asks_);
        return out;
    }
    const auto split = asks_.begin() + ptrdiff_t(maxCount);
    out.assign(asks_.begin(), split);
    asks_.erase(asks_.begin(), split);
    return out;
}

void PairingAskQueue::Answer(uint64_t addrPacked, bool allowed) {
    if (addrPacked == 0) return;
    const std::lock_guard<std::mutex> lock(mutex_);
    asks_.erase(std::remove_if(asks_.begin(), asks_.end(),
                    [addrPacked](const PairingAsk& ask) { return ask.addrPacked == addrPacked; }),
        asks_.end());
    answers_.erase(std::remove_if(answers_.begin(), answers_.end(),
                       [addrPacked](const auto& row) { return row.first == addrPacked; }),
        answers_.end());
    answers_.emplace_back(addrPacked, allowed);
}

bool PairingAskQueue::TakeAnswer(uint64_t addrPacked, bool& allowed) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = answers_.begin(); it != answers_.end(); ++it) {
        if (it->first != addrPacked) continue;
        allowed = it->second;
        answers_.erase(it);
        return true;
    }
    return false;
}

void PairingAskQueue::Clear() {
    const std::lock_guard<std::mutex> lock(mutex_);
    asks_.clear();
    answers_.clear();
}

PairingAskQueue& SharedPairingAskQueue() {
    static PairingAskQueue queue;
    return queue;
}

}
