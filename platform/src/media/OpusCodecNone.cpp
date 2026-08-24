#include "deskhubp/media/OpusCodec.h"
#include "deskhubp/diag/Log.h"

namespace deskhubp {

using deskhub::media::AudioFormat;

namespace {

void WarnSilentBuild() {
    LOGW("[audio] evt=codec_absent note=built without third_party/opus, this build has no sound");
}

}

OpusAudioEncoder::~OpusAudioEncoder() = default;

bool OpusAudioEncoder::Open(const AudioFormat&, uint32_t) {
    WarnSilentBuild();
    return false;
}

void OpusAudioEncoder::Close() {}

size_t OpusAudioEncoder::Encode(std::span<const int16_t>, std::span<uint8_t>) {
    return 0;
}

bool OpusAudioEncoder::SetBitrate(uint32_t) {
    return false;
}

const char* OpusAudioEncoder::BackendName() {
    return "none";
}

OpusAudioDecoder::~OpusAudioDecoder() = default;

bool OpusAudioDecoder::Open(const AudioFormat&) {
    WarnSilentBuild();
    return false;
}

void OpusAudioDecoder::Close() {}

size_t OpusAudioDecoder::Decode(std::span<const uint8_t>, std::span<int16_t>) {
    return 0;
}

size_t OpusAudioDecoder::Conceal(std::span<int16_t>) {
    return 0;
}

}
