#include "PwStream.h"

#include "deskhub/media/PcmRing.h"
#include "deskhubp/audio/AudioSink.h"

#include <pipewire/version.h>

#include <algorithm>

namespace deskhubp {

struct AudioSink::Impl {
    deskhub::media::AudioFormat format{};
    deskhub::media::PcmRing ring;

    static void OnProcess(void* userdata);
    static void OnStateChanged(void* userdata, pw_stream_state old, pw_stream_state now,
        const char* error);
    static const pw_stream_events kEvents;

    PwStream pw;
};

void AudioSink::Impl::OnProcess(void* userdata) {
    auto* impl = static_cast<AudioSink::Impl*>(userdata);
    pw_buffer* b = pw_stream_dequeue_buffer(impl->pw.stream());
    if (b == nullptr) return;

    spa_data& d = b->buffer->datas[0];
    if (d.data == nullptr) {
        pw_stream_queue_buffer(impl->pw.stream(), b);
        return;
    }

    const size_t stride = sizeof(int16_t) * impl->format.channels;
    size_t wantedSamples = d.maxsize / sizeof(int16_t);
#if PW_CHECK_VERSION(0, 3, 49)
    if (b->requested != 0) {
        const size_t requested = size_t(b->requested) * impl->format.channels;
        wantedSamples = std::min(wantedSamples, requested);
    }
#else
    wantedSamples = std::min(wantedSamples, impl->format.interleavedSamples());
#endif

    auto* out = static_cast<int16_t*>(d.data);
    impl->ring.TakeOrSilence(out, wantedSamples);

    d.chunk->offset = 0;
    d.chunk->stride = uint32_t(stride);
    d.chunk->size = uint32_t(wantedSamples * sizeof(int16_t));
    pw_stream_queue_buffer(impl->pw.stream(), b);
}

void AudioSink::Impl::OnStateChanged(void* userdata, pw_stream_state old, pw_stream_state now,
    const char* error) {
    (void)userdata;
    if (now == PW_STREAM_STATE_ERROR) LogPwStreamError("sink_error", old, error);
}

const pw_stream_events AudioSink::Impl::kEvents =
    MakePwStreamEvents(&AudioSink::Impl::OnProcess, &AudioSink::Impl::OnStateChanged);

AudioSink::AudioSink() = default;

AudioSink::~AudioSink() {
    Close();
}

bool AudioSink::Open(const deskhub::media::AudioFormat& format) {
    Close();
    if (!deskhub::media::IsSupportedAudioFormat(format)) return false;

    auto impl = std::make_unique<Impl>();
    impl->format = format;
    impl->ring.Open(format);

    const PwStreamOptions options{"deskhub-audio", "Deskhub", "deskhub", "Playback", false,
        PW_DIRECTION_OUTPUT, "sink_open_fail", "sink_connect_fail", true};
    if (!impl->pw.Open(options, Impl::kEvents, impl.get(), format)) return false;

    impl_ = std::move(impl);
    LOGI("[audio] evt=sink_open backend=pipewire rate=%u ch=%u", format.sampleRate,
        format.channels);
    return true;
}

void AudioSink::Close() {
    if (!impl_) return;
    impl_->pw.Close();
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
    return "pipewire";
}

}
