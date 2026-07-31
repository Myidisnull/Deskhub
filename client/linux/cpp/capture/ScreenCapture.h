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

    bool Start(int portalFd, uint32_t nodeId, uint32_t fps, FrameHandler onFrame);
    void Stop();

    bool Closed() const;

    bool usingDmaBuf() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};
