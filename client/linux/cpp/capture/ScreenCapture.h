#pragma once
#include <cstdint>
#include <functional>
#include <memory>

#include "capture/CaptureTypes.h"

class ScreenCapture {
public:
    using FrameHandler = std::function<void(const LinuxFrameInfo&)>;

    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    bool Start(uint64_t targetId, const deskhub::media::CaptureOptions& opt,
        FrameHandler onFrame);
    void Stop();

    bool Closed() const;

    bool usingDmaBuf() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

static_assert(deskhub::media::ScreenCaptureLike<ScreenCapture>,
    "ScreenCapture must match the shared capture signature");
static_assert(deskhub::media::ZeroCopyCapture<ScreenCapture>,
    "the PipeWire capture reports whether the frame stayed in GPU memory");
