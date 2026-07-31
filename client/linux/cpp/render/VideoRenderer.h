#pragma once
#include <va/va.h>
#include <va/va_drmcommon.h>

#include <atomic>
#include <cstdint>
#include <mutex>

#include "render/VideoSink.h"

class VideoRenderer : public VideoSink {
public:
    VideoRenderer() = default;
    ~VideoRenderer() override;
    VideoRenderer(const VideoRenderer&) = delete;
    VideoRenderer& operator=(const VideoRenderer&) = delete;

    void SubmitFrame(void* avFrame, uint64_t ptsUs) override;
    void SetVaDisplay(void* vaDisplay) override;
    void DropFrames() override;

    bool Realize();
    void Unrealize();
    bool Render(int viewW, int viewH);

    void ClearBlack();

    uint32_t TakeRenderedCount() override {
        return rendered_.exchange(0, std::memory_order_relaxed);
    }
    uint64_t lastRenderedPtsUs() const override {
        return lastRenderedPts_.load(std::memory_order_relaxed);
    }
    bool hasFrame() const {
        return hasFrame_.load(std::memory_order_acquire);
    }

private:
    void ClearSlotLocked();
    bool UploadLocked();

    std::mutex mutex_;
    void* frame_ = nullptr;
    bool dmabuf_ = false;
    VADRMPRIMESurfaceDescriptor desc_{};
    uint64_t ptsUs_ = 0;
    std::atomic<bool> hasFrame_{false};

    void* vaDisplay_ = nullptr;

    unsigned program_ = 0;
    unsigned vao_ = 0, vbo_ = 0;
    unsigned tex_[3] = {0, 0, 0};
    int uPlanarUv_ = -1;
    bool glReady_ = false;

    std::atomic<uint32_t> rendered_{0};
    std::atomic<uint64_t> lastRenderedPts_{0};
};
