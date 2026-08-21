#include "deskhubp/audio/AudioCapture.h"

#include <utility>

namespace deskhubp {

struct AudioCapture::Impl {};

AudioCapture::AudioCapture() = default;
AudioCapture::~AudioCapture() = default;

bool AudioCapture::Start(const deskhub::media::AudioFormat&, FrameHandler onFrame) {
    const FrameHandler discarded = std::move(onFrame);
    return false;
}

void AudioCapture::Stop() {}

bool AudioCapture::Running() const {
    return false;
}

uint64_t AudioCapture::framesCaptured() const {
    return 0;
}

uint64_t AudioCapture::framesPaddedWithSilence() const {
    return 0;
}

const char* AudioCapture::BackendName() {
    return "none";
}

}
