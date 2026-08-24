#include "deskhubp/audio/AudioSink.h"

namespace deskhubp {

struct AudioSink::Impl {};

AudioSink::AudioSink() = default;
AudioSink::~AudioSink() = default;

bool AudioSink::Open(const deskhub::media::AudioFormat&) {
    return false;
}

void AudioSink::Close() {}

bool AudioSink::Write(std::span<const int16_t>) {
    return false;
}

bool AudioSink::IsOpen() const {
    return false;
}

size_t AudioSink::framesQueued() const {
    return 0;
}

uint64_t AudioSink::framesDropped() const {
    return 0;
}

uint64_t AudioSink::framesStarved() const {
    return 0;
}

const char* AudioSink::BackendName() {
    return "none";
}

}
