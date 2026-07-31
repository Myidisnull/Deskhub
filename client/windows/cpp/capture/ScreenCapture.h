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

    void SetDevice(ID3D11Device* device);

    bool Start(uint64_t targetId, const deskhub::media::CaptureOptions& opt,
        FrameHandler onFrame);
    void Stop();

    bool Closed() const;

    ID3D11Device* Device() const;
    ID3D11DeviceContext* Context() const;

private:
    ID3D11Device* pendingDevice_ = nullptr;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

static_assert(deskhub::media::ScreenCaptureLike<ScreenCapture>,
    "ScreenCapture must match the shared capture signature");
