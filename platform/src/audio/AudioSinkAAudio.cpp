#include "deskhub/media/PcmRing.h"
#include "deskhubp/audio/AudioSink.h"
#include "deskhubp/diag/Log.h"

#include <aaudio/AAudio.h>

namespace deskhubp {

struct AudioSink::Impl {
    AAudioStream* stream = nullptr;
    deskhub::media::AudioFormat format{};

    deskhub::media::PcmRing ring;

    static aaudio_data_callback_result_t OnData(AAudioStream*, void* userData, void* audioData,
        int32_t numFrames);
};

aaudio_data_callback_result_t AudioSink::Impl::OnData(AAudioStream*, void* userData,
    void* audioData, int32_t numFrames) {
    auto* impl = static_cast<AudioSink::Impl*>(userData);
    auto* out = static_cast<int16_t*>(audioData);
    const size_t wanted = size_t(numFrames) * impl->format.channels;
    impl->ring.TakeOrSilence(out, wanted);
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
    impl->ring.Open(format);

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
    impl_->ring.Put(pcm);
    return true;
}

bool AudioSink::IsOpen() const {
    return impl_ != nullptr;
}

size_t AudioSink::framesQueued() const {
    return impl_ ? impl_->ring.framesQueued() : 0;
}

uint64_t AudioSink::framesDropped() const {
    return impl_ ? impl_->ring.framesDropped() : 0;
}

uint64_t AudioSink::framesStarved() const {
    return impl_ ? impl_->ring.framesStarved() : 0;
}

const char* AudioSink::BackendName() {
    return "aaudio";
}

}
