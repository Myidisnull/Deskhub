#pragma once
#include <cstdint>
#include <memory>

#include "capture/CaptureTypes.h"
#include "encode/VaEncoder.h"

#include "deskhub/media/VideoContract.h"
#include "deskhub/media/VideoTypes.h"

#ifdef DESKHUB_HAVE_NVENC
#include "encode/NvEncoder.h"
#endif

class HwEncoder {
public:
    bool Init(const deskhub::media::EncoderConfig& cfg, FrameMemory frameKind) {
#ifdef DESKHUB_HAVE_NVENC
        if (frameKind == FrameMemory::Mapped && NvEncoder::DriverPresent()) {
            auto nv = std::make_unique<NvEncoder>();
            if (nv->Init(cfg)) {
                nv_ = std::move(nv);
                return true;
            }
        }
#else
        (void)frameKind;
#endif
        auto va = std::make_unique<VaEncoder>();
        if (!va->Init(cfg)) return false;
        va_ = std::move(va);
        return true;
    }

    bool Encode(const LinuxFrameInfo& fi, uint64_t timestampUs, bool forceKeyframe) {
#ifdef DESKHUB_HAVE_NVENC
        if (nv_) {
            if (fi.memory != FrameMemory::Mapped) return false;
            return nv_->Encode(fi, timestampUs, forceKeyframe);
        }
#endif
        return va_ && va_->Encode(fi, timestampUs, forceKeyframe);
    }

    bool EncodeLast(uint64_t timestampUs, bool forceKeyframe) {
#ifdef DESKHUB_HAVE_NVENC
        if (nv_) return nv_->EncodeLast(timestampUs, forceKeyframe);
#endif
        return va_ && va_->EncodeLast(timestampUs, forceKeyframe);
    }

    bool haveSourceFrame() const {
#ifdef DESKHUB_HAVE_NVENC
        if (nv_) return nv_->haveSourceFrame();
#endif
        return va_ && va_->haveSourceFrame();
    }

    bool SetBitrate(uint32_t bitrateBps) {
#ifdef DESKHUB_HAVE_NVENC
        if (nv_) return nv_->SetBitrate(bitrateBps);
#endif
        return va_ && va_->SetBitrate(bitrateBps);
    }

    void Finish() {
#ifdef DESKHUB_HAVE_NVENC
        if (nv_) nv_->Finish();
#endif
        if (va_) va_->Finish();
    }

    bool IsOpen() const {
#ifdef DESKHUB_HAVE_NVENC
        if (nv_) return nv_->IsOpen();
#endif
        return va_ && va_->IsOpen();
    }

    const char* BackendName() const {
#ifdef DESKHUB_HAVE_NVENC
        if (nv_) return nv_->BackendName();
#endif
        return va_ ? va_->BackendName() : "none";
    }

private:
#ifdef DESKHUB_HAVE_NVENC
    std::unique_ptr<NvEncoder> nv_;
#endif
    std::unique_ptr<VaEncoder> va_;
};

static_assert(deskhub::media::VideoEncoderLike<HwEncoder, const LinuxFrameInfo&>,
    "HwEncoder must match the shared encoder signature");
