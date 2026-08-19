#include "deskhubp/audio/AudioPlayer.h"

#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"

#include <vector>

namespace deskhubp {

AudioPlayer::~AudioPlayer() {
    Stop();
}

bool AudioPlayer::Start(const deskhub::media::AudioFormat& format, uint32_t targetDelayMs) {
    Stop();
    if (!deskhub::media::IsSupportedAudioFormat(format)) return false;
    if (!decoder_.Open(format)) return false;
    if (!sink_.Open(format)) {
        decoder_.Close();
        return false;
    }

    format_ = format;
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        buffer_ = deskhub::AudioJitterBuffer(targetDelayMs);
    }
    decodeFailures_.store(0, std::memory_order_relaxed);
    quit_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { Run(); });
    LOGI("[audio] evt=player_start delay=%ums sink=%s", targetDelayMs, AudioSink::BackendName());
    return true;
}

void AudioPlayer::Stop() {
    quit_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    running_.store(false, std::memory_order_release);
    sink_.Close();
    decoder_.Close();
}

void AudioPlayer::Push(const deskhub::AudioPacketView& packet) {
    if (!running_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(bufferMutex_);
    buffer_.Push(packet);
}

void AudioPlayer::Run() {
    std::vector<int16_t> pcm(format_.interleavedSamples());
    const uint64_t frameUs = uint64_t(format_.samplesPerFrame) * 1'000'000 / format_.sampleRate;
    uint64_t nextUs = NowUs();

    while (!quit_.load(std::memory_order_acquire)) {
        std::optional<deskhub::AudioJitterBuffer::Frame> frame;
        {
            std::lock_guard<std::mutex> lock(bufferMutex_);
            frame = buffer_.Pop();
        }

        if (frame) {
            const size_t samples = Fill(*frame, pcm);
            if (samples == format_.samplesPerFrame)
                sink_.Write(pcm);
            else
                decodeFailures_.fetch_add(1, std::memory_order_relaxed);
        }

        nextUs += frameUs;
        const uint64_t nowUs = NowUs();
        if (nextUs > nowUs)
            SleepUs(nextUs - nowUs);
        else
            nextUs = nowUs;
    }
}

size_t AudioPlayer::Fill(const deskhub::AudioJitterBuffer::Frame& frame,
    std::span<int16_t> pcm) {
    if (frame.concealed) return decoder_.Conceal(pcm);
    return decoder_.Decode(frame.payload, pcm);
}

AudioPlayer::Stats AudioPlayer::stats() const {
    Stats out;
    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        out.jitter = buffer_.stats();
    }
    out.decodeFailures = decodeFailures_.load(std::memory_order_relaxed);
    out.sinkDropped = sink_.framesDropped();
    out.sinkStarved = sink_.framesStarved();
    return out;
}

}
