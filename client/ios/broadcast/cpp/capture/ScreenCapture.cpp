#include "capture/ScreenCapture.h"

#include <mutex>
#include <utility>

namespace {

std::mutex g_activeMutex;
ScreenCapture* g_active = nullptr;

}

ScreenCapture::~ScreenCapture() {
    Stop();
}

bool ScreenCapture::Start(const deskhub::media::CaptureOptions&, FrameHandler onFrame) {
    onFrame_ = std::move(onFrame);
    finished_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);

    std::lock_guard<std::mutex> lk(g_activeMutex);
    g_active = this;
    return true;
}

void ScreenCapture::Stop() {
    std::lock_guard<std::mutex> lk(g_activeMutex);
    if (g_active == this) g_active = nullptr;
    running_.store(false, std::memory_order_release);
}

bool ScreenCapture::Closed() const {
    return !running_.load(std::memory_order_acquire) ||
           finished_.load(std::memory_order_acquire);
}

void ScreenCapture::DeliverFrame(const Frame& frame) {
    std::lock_guard<std::mutex> lk(g_activeMutex);
    if (!g_active || !g_active->onFrame_) return;
    if (!g_active->running_.load(std::memory_order_acquire)) return;
    g_active->onFrame_(frame);
}

void ScreenCapture::ReportBroadcastFinished() {
    std::lock_guard<std::mutex> lk(g_activeMutex);
    if (g_active) g_active->finished_.store(true, std::memory_order_release);
}
