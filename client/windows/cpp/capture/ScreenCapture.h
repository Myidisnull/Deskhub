#pragma once
#include "capture/CaptureTypes.h"
#include <functional>
#include <memory>

namespace capture {

void InitRuntime();

}

class ScreenCapture {
public:
    using FrameHandler = std::function<void(const FrameInfo&)>;

    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    bool Start(HMONITOR monitor, ID3D11Device* device, FrameHandler onFrame);
    void Stop();

    bool Closed() const;

    ID3D11Device* Device() const;
    ID3D11DeviceContext* Context() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
