#pragma once
#include <android/native_window.h>

#include <atomic>
#include <cstdint>
#include <functional>

#include "deskhub/media/CaptureContract.h"

class ScreenCapture {
public:
    using DisplaySizeHandler = std::function<void(uint32_t, uint32_t)>;

    ScreenCapture() = default;
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    bool Start(const deskhub::media::CaptureOptions& opt, DisplaySizeHandler onDisplaySize);

    void Stop();

    bool Closed() const;

    bool AttachEncoderSurface(ANativeWindow* window, uint32_t width, uint32_t height);

    void DetachEncoderSurface();

    static void ReportDisplaySize(uint32_t width, uint32_t height);

    static void ReportProjectionStopped();

private:
    std::atomic<bool> running_{false};
    std::atomic<bool> projectionStopped_{false};
    std::atomic<bool> surfaceAttached_{false};
    DisplaySizeHandler onDisplaySize_;
};

static_assert(deskhub::media::ScreenCaptureLike<ScreenCapture>,
    "ScreenCapture must match the shared capture signature");
