#include "capture/AudioCapture.h"

#include "deskhubp/diag/Log.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

std::once_flag g_pwInit;

}

struct AudioCapture::Impl {
    pw_thread_loop* loop = nullptr;
    pw_stream* stream = nullptr;
    deskhub::media::AudioFormat format{};
    FrameHandler onFrame;

    std::vector<int16_t> staging;
    size_t staged = 0;
    std::atomic<uint64_t> frames{0};

    void Feed(const int16_t* samples, size_t count) {
        while (count > 0) {
            const size_t room = staging.size() - staged;
            const size_t take = std::min(room, count);
            std::memcpy(staging.data() + staged, samples, take * sizeof(int16_t));
            staged += take;
            samples += take;
            count -= take;
            if (staged < staging.size()) return;
            if (onFrame) onFrame(staging);
            frames.fetch_add(1, std::memory_order_relaxed);
            staged = 0;
        }
    }

    static void OnProcess(void* userdata);
    static void OnStateChanged(void* userdata, pw_stream_state old, pw_stream_state now,
        const char* error);
    static const pw_stream_events kEvents;
};

void AudioCapture::Impl::OnProcess(void* userdata) {
    auto* impl = static_cast<AudioCapture::Impl*>(userdata);
    pw_buffer* b = pw_stream_dequeue_buffer(impl->stream);
    if (b == nullptr) return;

    const spa_data& d = b->buffer->datas[0];
    if (d.data != nullptr && d.chunk->size > 0) {
        const auto* samples = static_cast<const int16_t*>(d.data);
        const size_t offset = d.chunk->offset / sizeof(int16_t);
        impl->Feed(samples + offset, d.chunk->size / sizeof(int16_t));
    }
    pw_stream_queue_buffer(impl->stream, b);
}

void AudioCapture::Impl::OnStateChanged(void*, pw_stream_state old, pw_stream_state now,
    const char* error) {
    if (now == PW_STREAM_STATE_ERROR)
        LOGE("[audio] evt=capture_error from=%s error=%s", pw_stream_state_as_string(old),
            error != nullptr ? error : "unknown");
}

const pw_stream_events AudioCapture::Impl::kEvents = {
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

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() {
    Stop();
}

bool AudioCapture::Start(const deskhub::media::AudioFormat& format, FrameHandler onFrame) {
    Stop();
    if (!deskhub::media::IsSupportedAudioFormat(format) || !onFrame) return false;

    std::call_once(g_pwInit, [] { pw_init(nullptr, nullptr); });

    auto impl = std::make_unique<Impl>();
    impl->format = format;
    impl->onFrame = std::move(onFrame);
    impl->staging.assign(format.interleavedSamples(), 0);

    impl->loop = pw_thread_loop_new("deskhub-audio-capture", nullptr);
    if (impl->loop == nullptr) {
        LOGE("[audio] evt=capture_open_fail step=loop");
        return false;
    }

    char latency[32];
    std::snprintf(latency, sizeof(latency), "%u/%u", format.samplesPerFrame, format.sampleRate);
    pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
        "Capture", PW_KEY_MEDIA_ROLE, "Communication", PW_KEY_STREAM_CAPTURE_SINK, "true",
        PW_KEY_APP_NAME, "Deskhub", PW_KEY_NODE_NAME, "deskhub-share", PW_KEY_NODE_LATENCY,
        latency, nullptr);

    pw_thread_loop_lock(impl->loop);
    impl->stream = pw_stream_new_simple(pw_thread_loop_get_loop(impl->loop), "Deskhub share",
        props, &Impl::kEvents, impl.get());
    if (impl->stream == nullptr) {
        pw_thread_loop_unlock(impl->loop);
        pw_thread_loop_destroy(impl->loop);
        LOGE("[audio] evt=capture_open_fail step=stream");
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

    const int rc = pw_stream_connect(impl->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
        pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                        PW_STREAM_FLAG_RT_PROCESS),
        params, 1);
    pw_thread_loop_unlock(impl->loop);

    if (rc < 0) {
        LOGW("[audio] evt=capture_connect_fail err=%s", spa_strerror(rc));
        pw_stream_destroy(impl->stream);
        pw_thread_loop_destroy(impl->loop);
        return false;
    }
    if (pw_thread_loop_start(impl->loop) < 0) {
        LOGE("[audio] evt=capture_open_fail step=start");
        pw_stream_destroy(impl->stream);
        pw_thread_loop_destroy(impl->loop);
        return false;
    }

    impl_ = std::move(impl);
    LOGI("[audio] evt=capture_open source=default sink monitor rate=%u ch=%u", format.sampleRate,
        format.channels);
    return true;
}

void AudioCapture::Stop() {
    if (!impl_) return;
    if (impl_->loop != nullptr) pw_thread_loop_stop(impl_->loop);
    if (impl_->stream != nullptr) pw_stream_destroy(impl_->stream);
    if (impl_->loop != nullptr) pw_thread_loop_destroy(impl_->loop);
    impl_.reset();
}

bool AudioCapture::Running() const {
    return impl_ != nullptr;
}

uint64_t AudioCapture::framesCaptured() const {
    return impl_ ? impl_->frames.load(std::memory_order_relaxed) : 0;
}
