#include "deskhubp/audio/AudioSink.h"
#include "deskhubp/diag/Log.h"

#include <aaudio/AAudio.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace deskhubp {

namespace {

constexpr size_t kRingFrames = 8;

}

struct AudioSink::Impl {
    AAudioStream* stream = nullptr;
    deskhub::media::AudioFormat format{};

    mutable std::mutex ringMutex;
    std::vector<int16_t> ring;
    size_t readAt = 0;
    size_t filled = 0;

    std::atomic<uint64_t> dropped{0};
    std::atomic<uint64_t> starved{0};

    size_t Take(int16_t* out, size_t wanted) {
        std::unique_lock<std::mutex> lock(ringMutex, std::try_to_lock);
        if (!lock.owns_lock()) return 0;
        const size_t take = std::min(wanted, filled);
        for (size_t i = 0; i < take; ++i) {
            out[i] = ring[readAt];
            readAt = (readAt + 1) % ring.size();
        }
        filled -= take;
        return take;
    }

    void Put(std::span<const int16_t> pcm) {
        std::lock_guard<std::mutex> lock(ringMutex);
        if (pcm.size() > ring.size()) return;
        while (ring.size() - filled < pcm.size()) {
            const size_t drop = std::min(pcm.size(), filled);
            readAt = (readAt + drop) % ring.size();
            filled -= drop;
            dropped.fetch_add(1, std::memory_order_relaxed);
        }
        size_t writeAt = (readAt + filled) % ring.size();
        for (int16_t s : pcm) {
            ring[writeAt] = s;
            writeAt = (writeAt + 1) % ring.size();
        }
        filled += pcm.size();
    }

    static aaudio_data_callback_result_t OnData(AAudioStream*, void* userData, void* audioData,
        int32_t numFrames);
};

aaudio_data_callback_result_t AudioSink::Impl::OnData(AAudioStream*, void* userData,
    void* audioData, int32_t numFrames) {
    auto* impl = static_cast<AudioSink::Impl*>(userData);
    auto* out = static_cast<int16_t*>(audioData);
    const size_t wanted = size_t(numFrames) * impl->format.channels;
    const size_t got = impl->Take(out, wanted);
    if (got < wanted) {
        std::memset(out + got, 0, (wanted - got) * sizeof(int16_t));
        impl->starved.fetch_add(1, std::memory_order_relaxed);
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

AudioSink::AudioSink() = default;

AudioSink::~AudioSink() {
    Close();
}

bool AudioSink::Open(const deskhub::media::AudioFormat& format) {
    Close();
    if (!deskhub::media::IsSupportedAudioFormat(format)) return false;

    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr) {
        LOGE("[audio] evt=sink_open_fail backend=aaudio step=builder");
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->format = format;
    impl->ring.assign(kRingFrames * format.interleavedSamples(), 0);

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, int32_t(format.channels));
    AAudioStreamBuilder_setSampleRate(builder, int32_t(format.sampleRate));
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setDataCallback(builder, Impl::OnData, impl.get());

    AAudioStream* stream = nullptr;
    const aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (opened != AAUDIO_OK || stream == nullptr) {
        LOGE("[audio] evt=sink_open_fail backend=aaudio err=%s", AAudio_convertResultToText(opened));
        return false;
    }

    impl->stream = stream;
    if (AAudioStream_requestStart(stream) != AAUDIO_OK) {
        LOGE("[audio] evt=sink_start_fail backend=aaudio");
        AAudioStream_close(stream);
        return false;
    }

    impl_ = std::move(impl);
    LOGI("[audio] evt=sink_open backend=aaudio rate=%u ch=%u", format.sampleRate, format.channels);
    return true;
}

void AudioSink::Close() {
    if (!impl_) return;
    if (impl_->stream != nullptr) {
        AAudioStream_requestStop(impl_->stream);
        AAudioStream_close(impl_->stream);
    }
    impl_.reset();
}

bool AudioSink::Write(std::span<const int16_t> pcm) {
    if (!impl_ || pcm.empty()) return false;
    if (pcm.size() != impl_->format.interleavedSamples()) return false;
    impl_->Put(pcm);
    return true;
}

bool AudioSink::IsOpen() const {
    return impl_ != nullptr;
}

size_t AudioSink::framesQueued() const {
    if (!impl_) return 0;
    std::lock_guard<std::mutex> lock(impl_->ringMutex);
    return impl_->filled / impl_->format.interleavedSamples();
}

uint64_t AudioSink::framesDropped() const {
    return impl_ ? impl_->dropped.load(std::memory_order_relaxed) : 0;
}

uint64_t AudioSink::framesStarved() const {
    return impl_ ? impl_->starved.load(std::memory_order_relaxed) : 0;
}

const char* AudioSink::BackendName() {
    return "aaudio";
}

}
