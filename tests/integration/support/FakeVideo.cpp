#include "support/FakeVideo.h"

#include "deskhubp/system/Clock.h"

namespace fake {

std::vector<uint8_t> FrameBytes(uint32_t frameIndex) {
    std::vector<uint8_t> out(kFrameBytes);
    deskhub::PutU32(out.data(), frameIndex);
    for (size_t i = 4; i < out.size(); ++i) out[i] = uint8_t(frameIndex * 31u + uint32_t(i));
    return out;
}

bool FrameIndexOf(std::span<const uint8_t> frame, uint32_t& indexOut) {
    if (frame.size() != kFrameBytes) return false;
    const uint32_t index = deskhub::GetU32(frame.data());
    for (size_t i = 4; i < frame.size(); ++i)
        if (frame[i] != uint8_t(index * 31u + uint32_t(i))) return false;
    indexOut = index;
    return true;
}

bool Capture::Start(uint32_t width, uint32_t height, uint32_t fps, FrameHandler onFrame) {
    Stop();
    width_ = width;
    height_ = height;
    fps_ = fps ? fps : 60;
    onFrame_ = std::move(onFrame);
    quit_.store(false);
    closed_.store(false);
    thread_ = std::thread([this] { Run(); });
    return true;
}

void Capture::Stop() {
    quit_.store(true);
    if (thread_.joinable()) thread_.join();
}

void Capture::Run() {
    const uint64_t periodUs = 1'000'000ull / fps_;
    uint64_t frameId = 0;
    while (!quit_.load()) {
        deskhub::media::FrameMeta meta;
        meta.width = width_;
        meta.height = height_;
        meta.timestampUs = NowUs();
        meta.frameId = frameId++;
        if (onFrame_) onFrame_(meta);
        SleepUs(periodUs);
    }
}

bool Encoder::Init(const deskhub::media::EncoderConfig& cfg) {
    onPacket_ = cfg.onPacket;
    open_ = true;
    Host().lastBitrateBps.store(cfg.bitrateBps, std::memory_order_relaxed);
    return true;
}

bool Encoder::Encode(const deskhub::media::FrameMeta& meta, uint64_t timestampUs,
    bool forceKeyframe) {
    if (!open_ || !onPacket_) return false;
    const uint32_t index = Host().framesEncoded.fetch_add(1, std::memory_order_relaxed);
    if (forceKeyframe) Host().keyframesEncoded.fetch_add(1, std::memory_order_relaxed);
    const std::vector<uint8_t> nal = FrameBytes(index);
    onPacket_(nal.data(), nal.size(), timestampUs ? timestampUs : meta.timestampUs,
        forceKeyframe);
    return true;
}

bool Encoder::SetBitrate(uint32_t bitrateBps) {
    Host().lastBitrateBps.store(bitrateBps, std::memory_order_relaxed);
    return true;
}

void Injector::Apply(const deskhub::InputEvent& e) {
    HostObservations& h = Host();
    std::lock_guard<std::mutex> lk(h.mutex);
    h.inputs.push_back(e);
}

void Injector::ReleaseAll() {
    Host().releaseAllCalls.fetch_add(1, std::memory_order_relaxed);
}

void HostObservations::Reset() {
    std::lock_guard<std::mutex> lk(mutex);
    inputs.clear();
    releaseAllCalls.store(0);
    lastBitrateBps.store(0);
    framesEncoded.store(0);
    keyframesEncoded.store(0);
}

size_t HostObservations::inputCount() {
    std::lock_guard<std::mutex> lk(mutex);
    return inputs.size();
}

std::vector<deskhub::InputEvent> HostObservations::TakeInputs() {
    std::lock_guard<std::mutex> lk(mutex);
    return inputs;
}

HostObservations& Host() {
    static HostObservations state;
    return state;
}

}
