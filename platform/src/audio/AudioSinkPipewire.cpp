#include "deskhubp/audio/AudioSink.h"
#include "deskhubp/diag/Log.h"

#include <pipewire/pipewire.h>
#include <pipewire/version.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace deskhubp {

namespace {

constexpr size_t kRingFrames = 8;
std::once_flag g_pwInit;

}

struct AudioSink::Impl {
    pw_thread_loop* loop = nullptr;
    pw_stream* stream = nullptr;
    spa_hook listener{};
    deskhub::media::AudioFormat format{};

    mutable std::mutex ringMutex;
    std::vector<int16_t> ring;
    size_t readAt = 0;
    size_t filled = 0;

    uint64_t dropped = 0;
    uint64_t starved = 0;

    static void OnProcess(void* userdata);
    static void OnStateChanged(void* userdata, pw_stream_state old, pw_stream_state now,
        const char* error);
    static const pw_stream_events kEvents;

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
            ++dropped;
        }
        size_t writeAt = (readAt + filled) % ring.size();
        for (int16_t s : pcm) {
            ring[writeAt] = s;
            writeAt = (writeAt + 1) % ring.size();
        }
        filled += pcm.size();
    }
};

void AudioSink::Impl::OnProcess(void* userdata) {
    auto* impl = static_cast<AudioSink::Impl*>(userdata);
    pw_buffer* b = pw_stream_dequeue_buffer(impl->stream);
    if (b == nullptr) return;

    spa_data& d = b->buffer->datas[0];
    if (d.data == nullptr) {
        pw_stream_queue_buffer(impl->stream, b);
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
    const size_t got = impl->Take(out, wantedSamples);
    if (got < wantedSamples) {
        std::memset(out + got, 0, (wantedSamples - got) * sizeof(int16_t));
        ++impl->starved;
    }

    d.chunk->offset = 0;
    d.chunk->stride = uint32_t(stride);
    d.chunk->size = uint32_t(wantedSamples * sizeof(int16_t));
    pw_stream_queue_buffer(impl->stream, b);
}

void AudioSink::Impl::OnStateChanged(void* userdata, pw_stream_state old,
    pw_stream_state now, const char* error) {
    (void)userdata;
    if (now == PW_STREAM_STATE_ERROR)
        LOGE("[audio] evt=sink_error from=%s error=%s", pw_stream_state_as_string(old),
            error != nullptr ? error : "unknown");
}

const pw_stream_events AudioSink::Impl::kEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .destroy = nullptr,
    .state_changed = OnStateChanged,
    .control_info = nullptr,
    .io_changed = nullptr,
    .param_changed = nullptr,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = OnProcess,
    .drained = nullptr,
    .command = nullptr,
    .trigger_done = nullptr,
};

AudioSink::AudioSink() = default;

AudioSink::~AudioSink() {
    Close();
}

bool AudioSink::Open(const deskhub::media::AudioFormat& format) {
    Close();
    if (!deskhub::media::IsSupportedAudioFormat(format)) return false;

    std::call_once(g_pwInit, [] { pw_init(nullptr, nullptr); });

    auto impl = std::make_unique<Impl>();
    impl->format = format;
    impl->ring.assign(kRingFrames * format.interleavedSamples(), 0);

    impl->loop = pw_thread_loop_new("deskhub-audio", nullptr);
    if (impl->loop == nullptr) {
        LOGE("[audio] evt=sink_open_fail step=loop");
        return false;
    }

    char latency[32];
    std::snprintf(latency, sizeof(latency), "%u/%u", format.samplesPerFrame, format.sampleRate);
    pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
        "Playback", PW_KEY_MEDIA_ROLE, "Communication", PW_KEY_APP_NAME, "Deskhub",
        PW_KEY_NODE_NAME, "deskhub", PW_KEY_NODE_LATENCY, latency, nullptr);

    pw_thread_loop_lock(impl->loop);
    impl->stream = pw_stream_new_simple(pw_thread_loop_get_loop(impl->loop), "Deskhub", props,
        &Impl::kEvents, impl.get());
    if (impl->stream == nullptr) {
        pw_thread_loop_unlock(impl->loop);
        pw_thread_loop_destroy(impl->loop);
        LOGE("[audio] evt=sink_open_fail step=stream");
        return false;
    }

    uint8_t podBuffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_S16;
    info.rate = format.sampleRate;
    info.channels = format.channels;
    const spa_pod* params[1] = {
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info),
    };

    const int rc = pw_stream_connect(impl->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                        PW_STREAM_FLAG_RT_PROCESS),
        params, 1);
    pw_thread_loop_unlock(impl->loop);

    if (rc < 0) {
        LOGE("[audio] evt=sink_connect_fail err=%s", spa_strerror(rc));
        pw_stream_destroy(impl->stream);
        pw_thread_loop_destroy(impl->loop);
        return false;
    }
    if (pw_thread_loop_start(impl->loop) < 0) {
        LOGE("[audio] evt=sink_open_fail step=start");
        pw_stream_destroy(impl->stream);
        pw_thread_loop_destroy(impl->loop);
        return false;
    }

    impl_ = std::move(impl);
    LOGI("[audio] evt=sink_open backend=pipewire rate=%u ch=%u", format.sampleRate,
        format.channels);
    return true;
}

void AudioSink::Close() {
    if (!impl_) return;
    if (impl_->loop != nullptr) pw_thread_loop_stop(impl_->loop);
    if (impl_->stream != nullptr) pw_stream_destroy(impl_->stream);
    if (impl_->loop != nullptr) pw_thread_loop_destroy(impl_->loop);
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
    return impl_ ? impl_->dropped : 0;
}

uint64_t AudioSink::framesStarved() const {
    return impl_ ? impl_->starved : 0;
}

const char* AudioSink::BackendName() {
    return "pipewire";
}

}
