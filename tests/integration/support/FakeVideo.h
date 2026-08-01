#pragma once
#include "deskhub/media/CaptureContract.h"
#include "deskhub/media/VideoTypes.h"
#include "deskhub/protocol/ByteOrder.h"
#include "deskhub/protocol/Wire.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace fake {

inline constexpr size_t kFrameBytes = 3500;

std::vector<uint8_t> FrameBytes(uint32_t frameIndex);

bool FrameIndexOf(std::span<const uint8_t> frame, uint32_t& indexOut);

class Capture {
public:
    using FrameHandler = std::function<void(const deskhub::media::FrameMeta&)>;

    ~Capture() {
        Stop();
    }

    bool Start(uint32_t width, uint32_t height, uint32_t fps, FrameHandler onFrame);
    void Stop();

    bool Closed() const {
        return closed_.load(std::memory_order_acquire);
    }

    void MarkClosed() {
        closed_.store(true, std::memory_order_release);
    }

private:
    void Run();

    uint32_t width_ = 0, height_ = 0, fps_ = 60;
    FrameHandler onFrame_;
    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> closed_{false};
};

class Encoder {
public:
    bool Init(const deskhub::media::EncoderConfig& cfg);

    bool Encode(const deskhub::media::FrameMeta& meta, uint64_t timestampUs, bool forceKeyframe);

    bool SetBitrate(uint32_t bitrateBps);

    void Finish() {
        open_ = false;
    }

    bool IsOpen() const {
        return open_;
    }

    const char* BackendName() const {
        return "fake";
    }

private:
    deskhub::media::PacketHandler onPacket_;
    bool open_ = false;
};

class Injector {
public:
    void Apply(const deskhub::InputEvent& e);
    void ReleaseAll();

    uint64_t skipped() const {
        return 0;
    }
};

struct HostObservations {
    std::mutex mutex;
    std::vector<deskhub::InputEvent> inputs;
    std::atomic<int> releaseAllCalls{0};
    std::atomic<uint32_t> lastBitrateBps{0};
    std::atomic<uint32_t> framesEncoded{0};
    std::atomic<uint32_t> keyframesEncoded{0};

    void Reset();
    size_t inputCount();
    std::vector<deskhub::InputEvent> TakeInputs();
};

HostObservations& Host();

}
