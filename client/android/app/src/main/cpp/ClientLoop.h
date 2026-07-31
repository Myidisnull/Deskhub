#pragma once
#include <android/native_window.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "decode/MediaCodecDecoder.h"
#include "deskhubp/net/UdpSocket.h"

#include "deskhub/control/ClockOffset.h"
#include "deskhub/diag/ClientDiag.h"
#include "deskhub/input/ClientInputQueue.h"
#include "deskhub/transport/Reassembler.h"
#include "deskhub/protocol/Wire.h"

class ClientLoop {
public:
    enum class Phase : int32_t { Idle = 0,
        Connecting = 1,
        Streaming = 2,
        Ended = 3 };

    ClientLoop() = default;
    ~ClientLoop();
    ClientLoop(const ClientLoop&) = delete;
    ClientLoop& operator=(const ClientLoop&) = delete;

    bool Start(const NetAddr& server, uint8_t sourceId, uint32_t screenW, uint32_t screenH);
    void Stop();

    void SetWindow(ANativeWindow* window);

    bool Finished() const {
        return finished_.load(std::memory_order_acquire);
    }

    Phase phase() const {
        return phase_.load(std::memory_order_acquire);
    }

    std::string StatusLine();

    std::string EndReason();

    void QueueKeyTap(int32_t vk, int32_t scan);

    void QueueKeyChord(int32_t modVk, int32_t modScan, int32_t vk, int32_t scan);

    void QueueMouseMoveAbs(int32_t nx, int32_t ny);

    void QueueMouseMoveRel(int32_t dx, int32_t dy);

    void QueueMouseButton(int32_t button, bool down);

    void QueueCharTap(uint32_t codepoint);

    uint32_t videoWidth() const {
        return negW_.load();
    }
    uint32_t videoHeight() const {
        return negH_.load();
    }

private:
    void NetThread();
    void DecodeThread();

    NetAddr server_{};
    uint8_t sourceId_ = 0;
    uint32_t screenW_ = 0, screenH_ = 0;
    UdpSocket sock_;

    std::thread netThread_;
    std::thread decodeThread_;

    std::atomic<bool> quit_{false};
    std::atomic<bool> finished_{false};
    std::atomic<Phase> phase_{Phase::Idle};

    std::mutex textMutex_;
    std::string statusLine_;
    std::string endReason_;

    std::atomic<uint32_t> negW_{0}, negH_{0};
    std::atomic<bool> rebuildDecoder_{false};

    std::mutex winMutex_;
    std::condition_variable winCv_;
    std::condition_variable winAckCv_;
    ANativeWindow* window_ = nullptr;
    uint64_t winGen_ = 0;
    uint64_t winAckGen_ = 0;
    bool decodeExited_ = false;

    static constexpr size_t kMaxQueuedFrames = 3;
    std::mutex decMutex_;
    std::condition_variable decCv_;
    std::deque<deskhub::Reassembler::Frame> decQueue_;

    std::atomic<bool> decodeFailed_{false};
    std::atomic<bool> displayCongested_{false};
    std::atomic<bool> queueOverflow_{false};
    std::atomic<uint32_t> stRendered_{0};

    deskhub::ClientInputQueue input_;

    deskhub::diag::ClientDiag diag_{deskhub::diag::ClientDiagCaps{
        false, true}};

    std::atomic<int64_t> lastE2eUs_{-1};
    deskhub::ClockOffset clockOffset_;
};
