#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace fake {

struct DecodeLog {
    std::mutex mutex;
    std::vector<std::vector<uint8_t>> frames;
    std::atomic<int> inits{0};
    std::atomic<int> shutdowns{0};
    std::atomic<int> width{0};
    std::atomic<int> height{0};

    void Reset();
    size_t frameCount();
    std::vector<std::vector<uint8_t>> TakeFrames();
};

DecodeLog& Decoded();

class Decoder {
public:
    bool Init(void* surface, int width, int height);

    bool Decode(const uint8_t* data, size_t size, uint64_t timestampUs);

    void Shutdown();

    bool IsOpen() const {
        return open_;
    }

    uint32_t TakeRenderedCount() {
        return rendered_.exchange(0);
    }

    uint64_t lastRenderedPtsUs() const {
        return lastPtsUs_.load();
    }

private:
    bool open_ = false;
    std::atomic<uint32_t> rendered_{0};
    std::atomic<uint64_t> lastPtsUs_{0};
};

}
