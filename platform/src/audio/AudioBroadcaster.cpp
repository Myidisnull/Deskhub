#include "deskhubp/audio/AudioBroadcaster.h"

#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"

namespace deskhubp {

AudioBroadcaster::~AudioBroadcaster() {
    Stop();
}

bool AudioBroadcaster::Start(SendFn send, const deskhub::media::AudioFormat& format,
    uint32_t bitrateBps) {
    Stop();
    if (!send || !deskhub::media::IsSupportedAudioFormat(format)) return false;

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
    running_.store(true, std::memory_order_release);
    LOGI("[audio] evt=broadcast_start rate=%u ch=%u bitrate=%u", format.sampleRate,
        format.channels, bitrateBps);
    return true;
}

void AudioBroadcaster::Stop() {
    running_.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(encoderMutex_);
    encoder_.Close();
    send_ = nullptr;
}

void AudioBroadcaster::Offer(std::span<const int16_t> pcm) {
    if (!running_.load(std::memory_order_acquire)) return;

    std::unique_lock<std::mutex> lock(encoderMutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        framesRefused_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!running_.load(std::memory_order_acquire) || !send_) return;

    const size_t written = encoder_.Encode(pcm, packet_);
    if (!written) {
        framesRefused_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const uint32_t seq = nextSeq_++;
    const uint64_t nowUs = NowUs();
    send_(std::span<const uint8_t>(packet_.data(), written), seq, nowUs);
    framesEncoded_.fetch_add(1, std::memory_order_relaxed);
    bytesEncoded_ += written;

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
