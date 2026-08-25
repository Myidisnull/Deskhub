#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace deskhubp {

struct PairingAsk {
    uint64_t addrPacked = 0;
    std::string shortKey{};
    std::string name{};
};

class PairingAskQueue {
public:
    void Push(PairingAsk ask);
    std::vector<PairingAsk> Take(size_t maxCount);
    void Answer(uint64_t addrPacked, bool allowed);
    bool TakeAnswer(uint64_t addrPacked, bool& allowed);
    void Clear();

private:
    static constexpr size_t kMaxQueued = 32;

    std::mutex mutex_;
    std::vector<PairingAsk> asks_;
    std::vector<std::pair<uint64_t, bool>> answers_;
};

PairingAskQueue& SharedPairingAskQueue();

}
