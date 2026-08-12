#include "capture/ScreenCapture.h"

#include <mutex>

#include "HostBridge.h"
#include "deskhubp/diag/Log.h"

namespace {

std::mutex g_activeMutex;
ScreenCapture* g_active = nullptr;

}

ScreenCapture::~ScreenCapture() {
    Stop();
}

bool ScreenCapture::Start(const deskhub::media::CaptureOptions&,
    DisplaySizeHandler onDisplaySize) {
    if (!deskhubj::HostProjectionReady()) {
        LOGE("[Capture] No live MediaProjection \xE2\x80\x94 the consent dialog was not accepted.");
        return false;
    }

    onDisplaySize_ = std::move(onDisplaySize);
    projectionStopped_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);

    std::lock_guard<std::mutex> lk(g_activeMutex);
    g_active = this;
    return true;
}

void ScreenCapture::Stop() {
    {
        std::lock_guard<std::mutex> lk(g_activeMutex);
        if (g_active == this) g_active = nullptr;
    }
    DetachEncoderSurface();
    running_.store(false, std::memory_order_release);
}

bool ScreenCapture::Closed() const {
    return !running_.load(std::memory_order_acquire) ||
           projectionStopped_.load(std::memory_order_acquire);
}

bool ScreenCapture::AttachEncoderSurface(ANativeWindow* window, uint32_t width, uint32_t height) {
    if (!window || !width || !height) return false;
    if (!deskhubj::AttachHostSurface(window, width, height)) {
        LOGE("[Capture] MediaProjection refused a %ux%u virtual display.", width, height);
        return false;
    }
    surfaceAttached_.store(true, std::memory_order_release);
    LOGI("[Capture] Virtual display %ux%u feeding the encoder input surface.", width, height);
    return true;
}

void ScreenCapture::DetachEncoderSurface() {
    if (!surfaceAttached_.exchange(false, std::memory_order_acq_rel)) return;
    deskhubj::DetachHostSurface();
}

void ScreenCapture::ReportDisplaySize(uint32_t width, uint32_t height) {
    DisplaySizeHandler handler;
    {
        std::lock_guard<std::mutex> lk(g_activeMutex);
        if (!g_active) return;
        handler = g_active->onDisplaySize_;
    }
    if (handler) handler(width, height);
}

void ScreenCapture::ReportProjectionStopped() {
    std::lock_guard<std::mutex> lk(g_activeMutex);
    if (g_active) g_active->projectionStopped_.store(true, std::memory_order_release);
}
