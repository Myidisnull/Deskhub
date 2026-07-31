#pragma once
#include <cstdint>

class VideoSink {
public:
    virtual ~VideoSink() = default;

    virtual void SubmitFrame(void* avFrame, uint64_t ptsUs) = 0;

    virtual void SetVaDisplay(void* vaDisplay) = 0;

    virtual void DropFrames() = 0;

    virtual uint32_t TakeRenderedCount() = 0;

    virtual uint64_t lastRenderedPtsUs() const = 0;
};
