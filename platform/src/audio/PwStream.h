#pragma once
#include "deskhub/media/AudioTypes.h"
#include "deskhubp/diag/Log.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#include <cstdio>
#include <mutex>

namespace deskhubp {

struct PwStreamOptions {
    const char* loopName;
    const char* streamName;
    const char* nodeName;
    const char* mediaCategory;
    bool captureSinkMonitor;
    pw_direction direction;
    const char* openFailEvent;
    const char* connectFailEvent;
    bool connectFailIsFatal;
};

inline pw_stream_events MakePwStreamEvents(void (*process)(void*),
    void (*stateChanged)(void*, pw_stream_state, pw_stream_state, const char*)) noexcept {
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed = stateChanged;
    events.process = process;
    return events;
}

inline void LogPwStreamError(const char* event, pw_stream_state from, const char* error) {
    LOGE("[audio] evt=%s from=%s error=%s", event, pw_stream_state_as_string(from),
        error != nullptr ? error : "unknown");
}

class PwStream {
public:
    ~PwStream() {
        Close();
    }

    bool Open(const PwStreamOptions& options, const pw_stream_events& events, void* userdata,
        const deskhub::media::AudioFormat& format) {
        static std::once_flag pwInit;
        std::call_once(pwInit, [] { pw_init(nullptr, nullptr); });

        loop_ = pw_thread_loop_new(options.loopName, nullptr);
        if (loop_ == nullptr) {
            LOGE("[audio] evt=%s step=loop", options.openFailEvent);
            return false;
        }

        char latency[32];
        std::snprintf(latency, sizeof(latency), "%u/%u", format.samplesPerFrame,
            format.sampleRate);
        pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, options.mediaCategory, PW_KEY_MEDIA_ROLE, "Communication",
            PW_KEY_APP_NAME, "Deskhub", PW_KEY_NODE_NAME, options.nodeName, PW_KEY_NODE_LATENCY,
            latency, nullptr);
        if (options.captureSinkMonitor)
            pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");

        pw_thread_loop_lock(loop_);
        stream_ = pw_stream_new_simple(pw_thread_loop_get_loop(loop_), options.streamName, props,
            &events, userdata);
        if (stream_ == nullptr) {
            pw_thread_loop_unlock(loop_);
            LOGE("[audio] evt=%s step=stream", options.openFailEvent);
            Close();
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

        const int rc = pw_stream_connect(stream_, options.direction, PW_ID_ANY,
            pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                            PW_STREAM_FLAG_RT_PROCESS),
            params, 1);
        pw_thread_loop_unlock(loop_);

        if (rc < 0) {
            if (options.connectFailIsFatal)
                LOGE("[audio] evt=%s err=%s", options.connectFailEvent, spa_strerror(rc));
            else
                LOGW("[audio] evt=%s err=%s", options.connectFailEvent, spa_strerror(rc));
            Close();
            return false;
        }
        if (pw_thread_loop_start(loop_) < 0) {
            LOGE("[audio] evt=%s step=start", options.openFailEvent);
            Close();
            return false;
        }
        started_ = true;
        return true;
    }

    void Close() {
        if (started_) pw_thread_loop_stop(loop_);
        if (stream_ != nullptr) pw_stream_destroy(stream_);
        if (loop_ != nullptr) pw_thread_loop_destroy(loop_);
        started_ = false;
        stream_ = nullptr;
        loop_ = nullptr;
    }

    pw_stream* stream() const {
        return stream_;
    }

private:
    pw_thread_loop* loop_ = nullptr;
    pw_stream* stream_ = nullptr;
    bool started_ = false;
};

}
