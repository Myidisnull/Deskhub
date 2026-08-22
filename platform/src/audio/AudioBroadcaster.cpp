#include "deskhubp/audio/AudioBroadcaster.h"

#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"

#include <algorithm>

namespace deskhubp {

AudioBroadcaster::~AudioBroadcaster() {
    Stop();
}

bool AudioBroadcaster::Start(SendFn send, const deskhub::media::AudioFormat& format,
    uint32_t bitrateBps) {
    Stop();
    if (!send || !deskhub::media::IsSupportedAudioFormat(format)) return false;

    {
        std::lock_guard<std::mutex> lock(encoderMutex_);
        if (!encoder_.Open(format, bitrateBps)) return false;

        send_ = std::move(send);
        format_ = format;
        packet_.assign(kMaxOpusPacketBytes, 0);
        nextSeq_ = 0;
        bytesEncoded_ = 0;
        reportedAtUs_ = NowUs();
        nextReportUs_ = reportedAtUs_ + kReportIntervalUs;
        framesEncoded_.store(0, std::memory_order_relaxed);
        framesRefused_.store(0, std::memory_order_relaxed);
    }

    for (PendingFrame& slot : queue_) {
        slot.pcm.assign(format.interleavedSamples(), 0);
        slot.samples = 0;
        slot.capturedAtUs = 0;
    }
    queueWriteAt_.store(0, std::memory_order_relaxed);
    queueReadAt_.store(0, std::memory_order_relaxed);

    running_.store(true, std::memory_order_release);
    worker_ = std::thread([this] { WorkerLoop(); });
    LOGI("[audio] evt=broadcast_start rate=%u ch=%u bitrate=%u", format.sampleRate,
        format.channels, bitrateBps);
    return true;
}

void AudioBroadcaster::Stop() {
    running_.store(false, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(encoderMutex_);
    encoder_.Close();
    send_ = nullptr;
}

void AudioBroadcaster::Offer(std::span<const int16_t> pcm) {
    if (!running_.load(std::memory_order_acquire)) return;

    const uint32_t writeAt = queueWriteAt_.load(std::memory_order_relaxed);
    const uint32_t readAt = queueReadAt_.load(std::memory_order_acquire);
    if (writeAt - readAt >= kQueueDepth) {
        framesRefused_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    PendingFrame& slot = queue_[writeAt % kQueueDepth];
    if (pcm.size() > slot.pcm.size()) {
        framesRefused_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::copy(pcm.begin(), pcm.end(), slot.pcm.begin());
    slot.samples = pcm.size();
    slot.capturedAtUs = NowUs();
    queueWriteAt_.store(writeAt + 1, std::memory_order_release);
}

void AudioBroadcaster::WorkerLoop() {
    while (running_.load(std::memory_order_acquire)) {
        const uint32_t readAt = queueReadAt_.load(std::memory_order_relaxed);
        if (readAt == queueWriteAt_.load(std::memory_order_acquire)) {
            SleepUs(kWorkerIdlePollUs);
            continue;
        }
        PendingFrame& slot = queue_[readAt % kQueueDepth];
        EncodeAndSend(std::span<const int16_t>(slot.pcm.data(), slot.samples),
            slot.capturedAtUs);
        queueReadAt_.store(readAt + 1, std::memory_order_release);
    }
}

void AudioBroadcaster::EncodeAndSend(std::span<const int16_t> pcm, uint64_t capturedAtUs) {
    std::lock_guard<std::mutex> lock(encoderMutex_);
    if (!running_.load(std::memory_order_acquire) || !send_) return;

    const size_t written = encoder_.Encode(pcm, packet_);
    if (!written) {
        framesRefused_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const uint32_t seq = nextSeq_++;
    send_(std::span<const uint8_t>(packet_.data(), written), seq, capturedAtUs);
    framesEncoded_.fetch_add(1, std::memory_order_relaxed);
    bytesEncoded_ += written;

    const uint64_t nowUs = NowUs();
    if (nowUs < nextReportUs_) return;
    const uint64_t sinceUs = nowUs - reportedAtUs_;
    LOGI("[DIAG][audio] evt=share frames=%llu refused=%llu kbps=%.1f",
        (unsigned long long)framesEncoded_.load(std::memory_order_relaxed),
        (unsigned long long)framesRefused_.load(std::memory_order_relaxed),
        sinceUs ? double(bytesEncoded_) * 8.0 * 1000.0 / double(sinceUs) : 0.0);
    bytesEncoded_ = 0;
    reportedAtUs_ = nowUs;
    nextReportUs_ = nowUs + kReportIntervalUs;
}

}
