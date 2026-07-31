#pragma once
#include <functional>
#include <memory>

#include "capture/CaptureTypes.h"

class ScreenCapture {
public:
    using FrameHandler = std::function<void(const MacFrameInfo&)>;

    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    bool Start(uint32_t displayId, uint32_t fps, uint32_t maxDim, FrameHandler onFrame);
    void Stop();

    void SetClientSize(uint32_t clientW, uint32_t clientH, uint32_t& outW, uint32_t& outH);

    void SetQuality(uint32_t scalePct, uint32_t fps, uint32_t& outW, uint32_t& outH);

    uint32_t TakeIdleCount();

    bool Closed() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};
