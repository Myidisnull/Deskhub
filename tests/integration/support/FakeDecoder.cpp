#include "support/FakeDecoder.h"

namespace fake {

void DecodeLog::Reset() {
    std::lock_guard<std::mutex> lk(mutex);
    frames.clear();
    inits.store(0);
    shutdowns.store(0);
    width.store(0);
    height.store(0);
}

size_t DecodeLog::frameCount() {
    std::lock_guard<std::mutex> lk(mutex);
    return frames.size();
}

std::vector<std::vector<uint8_t>> DecodeLog::TakeFrames() {
    std::lock_guard<std::mutex> lk(mutex);
    return frames;
}

DecodeLog& Decoded() {
    static DecodeLog log;
    return log;
}

bool Decoder::Init(void* surface, int width, int height) {
    if (!surface) return false;
    open_ = true;
    DecodeLog& log = Decoded();
    log.width.store(width);
    log.height.store(height);
    log.inits.fetch_add(1);
    return true;
}

bool Decoder::Decode(const uint8_t* data, size_t size, uint64_t timestampUs) {
    if (!open_ || !data || !size) return false;
    DecodeLog& log = Decoded();
    {
        std::lock_guard<std::mutex> lk(log.mutex);
        log.frames.emplace_back(data, data + size);
    }
    rendered_.fetch_add(1);
    lastPtsUs_.store(timestampUs);
    return true;
}

void Decoder::Shutdown() {
    if (!open_) return;
    open_ = false;
    Decoded().shutdowns.fetch_add(1);
}

}
