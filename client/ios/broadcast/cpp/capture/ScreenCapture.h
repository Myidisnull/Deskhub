#pragma once
#include <atomic>
#include <cstdint>
#include <functional>

#include "deskhub/media/CaptureContract.h"

class ScreenCapture {
public:
    using Frame = deskhub::media::CapturedFrame<void*>;
    using FrameHandler = std::function<void(const Frame&)>;

    ScreenCapture() = default;
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    bool Start(const deskhub::media::CaptureOptions& opt, FrameHandler onFrame);

    void Stop();

    bool Closed() const;

    static void DeliverFrame(const Frame& frame);

    static void BeginBroadcast();

    static void ReportBroadcastFinished();

private:
    std::atomic<bool> running_{false};
    std::atomic<bool> finished_{false};
    FrameHandler onFrame_;
};

static_assert(deskhub::media::ScreenCaptureLike<ScreenCapture>,
    "ScreenCapture must match the shared capture signature");
