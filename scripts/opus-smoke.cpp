#include <opus/opus.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kFrameSamples = 960;
constexpr int kBitrateBps = 64000;
constexpr int kPacketLossPct = 5;
constexpr int kToneHz = 440;
constexpr int16_t kToneAmplitude = 12000;
constexpr int kToneFrames = 100;
constexpr int kSilenceFrames = 50;
constexpr int kWarmupFrames = 3;
constexpr int kMaxPacketBytes = 1275;
constexpr int16_t kCrossingHysteresis = 1000;

constexpr size_t kMaxDatagram = 1200;
constexpr size_t kCommonHeaderSize = 8;
constexpr size_t kAudioHeaderSize = 12;
constexpr size_t kMaxAudioPayload = kMaxDatagram - kCommonHeaderSize - kAudioHeaderSize;

std::vector<int16_t> MakeTone(int frames) {
    std::vector<int16_t> pcm(size_t(frames) * kFrameSamples * kChannels);
    for (int i = 0; i < frames * kFrameSamples; ++i) {
        const double t = double(i) / kSampleRate;
        const auto s = int16_t(kToneAmplitude * std::sin(2.0 * M_PI * kToneHz * t));
        pcm[size_t(i) * kChannels] = s;
        pcm[size_t(i) * kChannels + 1] = s;
    }
    return pcm;
}

double Rms(const int16_t* samples, size_t count) {
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) sum += double(samples[i]) * double(samples[i]);
    return count == 0 ? 0.0 : std::sqrt(sum / double(count));
}

int CountRisingCrossings(const std::vector<int16_t>& interleaved) {
    int crossings = 0;
    bool high = false;
    for (size_t i = 0; i < interleaved.size(); i += kChannels) {
        if (!high && interleaved[i] > kCrossingHysteresis) {
            high = true;
            ++crossings;
        } else if (high && interleaved[i] < -kCrossingHysteresis) {
            high = false;
        }
    }
    return crossings;
}

}

int main() {
    int err = 0;
    OpusEncoder* enc = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_AUDIO, &err);
    if (enc == nullptr || err != OPUS_OK) {
        std::printf("FAIL: opus_encoder_create returned %s\n", opus_strerror(err));
        return 1;
    }
    OpusDecoder* dec = opus_decoder_create(kSampleRate, kChannels, &err);
    if (dec == nullptr || err != OPUS_OK) {
        std::printf("FAIL: opus_decoder_create returned %s\n", opus_strerror(err));
        return 1;
    }
    std::printf("PASS: %s created a 48 kHz stereo encoder and decoder\n",
        opus_get_version_string());

    opus_encoder_ctl(enc, OPUS_SET_BITRATE(kBitrateBps));
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(kPacketLossPct));
    opus_encoder_ctl(enc, OPUS_SET_DTX(1));

    const std::vector<int16_t> tone = MakeTone(kToneFrames);
    std::vector<int16_t> decoded;
    std::vector<uint8_t> packet(kMaxPacketBytes);
    size_t totalBytes = 0;
    int largestPacket = 0;

    for (int f = 0; f < kToneFrames; ++f) {
        const int16_t* in = tone.data() + size_t(f) * kFrameSamples * kChannels;
        const int written = opus_encode(enc, in, kFrameSamples, packet.data(), kMaxPacketBytes);
        if (written <= 0) {
            std::printf("FAIL: opus_encode on frame %d returned %s\n", f, opus_strerror(written));
            return 1;
        }
        totalBytes += size_t(written);
        if (written > largestPacket) largestPacket = written;

        std::vector<int16_t> out(size_t(kFrameSamples) * kChannels);
        const int samples = opus_decode(dec, packet.data(), written, out.data(), kFrameSamples, 0);
        if (samples != kFrameSamples) {
            std::printf("FAIL: opus_decode on frame %d returned %d, expected %d\n", f, samples,
                kFrameSamples);
            return 1;
        }
        if (f >= kWarmupFrames) decoded.insert(decoded.end(), out.begin(), out.end());
    }
    std::printf("PASS: %d frames of 20 ms round-tripped through encode and decode\n", kToneFrames);

    const double sourceRms = Rms(tone.data() + size_t(kWarmupFrames) * kFrameSamples * kChannels,
        decoded.size());
    const double decodedRms = Rms(decoded.data(), decoded.size());
    const double levelDb = 20.0 * std::log10(decodedRms / sourceRms);
    if (std::fabs(levelDb) > 2.0) {
        std::printf("FAIL: decoded level is %.2f dB off the source\n", levelDb);
        return 1;
    }
    std::printf("PASS: decoded level within %.2f dB of the source\n", levelDb);

    const double seconds = double(decoded.size() / kChannels) / kSampleRate;
    const double measuredHz = double(CountRisingCrossings(decoded)) / seconds;
    if (std::fabs(measuredHz - kToneHz) > 20.0) {
        std::printf("FAIL: decoded tone measured %.1f Hz, expected %d Hz\n", measuredHz, kToneHz);
        return 1;
    }
    std::printf("PASS: decoded tone measured %.1f Hz\n", measuredHz);

    std::vector<int16_t> concealed(size_t(kFrameSamples) * kChannels);
    const int plcSamples = opus_decode(dec, nullptr, 0, concealed.data(), kFrameSamples, 0);
    if (plcSamples != kFrameSamples) {
        std::printf("FAIL: packet-loss concealment returned %d, expected %d\n", plcSamples,
            kFrameSamples);
        return 1;
    }
    std::printf("PASS: packet-loss concealment filled a lost 20 ms frame\n");

    const double avgBytes = double(totalBytes) / kToneFrames;
    const double kbps = avgBytes * 8.0 * 50.0 / 1000.0;
    if (size_t(largestPacket) > kMaxAudioPayload) {
        std::printf("FAIL: largest packet %d bytes exceeds the %zu-byte datagram budget\n",
            largestPacket, kMaxAudioPayload);
        return 1;
    }
    std::printf("PASS: largest packet %d bytes fits the %zu-byte datagram budget\n", largestPacket,
        kMaxAudioPayload);
    std::printf("INFO: average %.1f bytes per 20 ms frame = %.1f kbps\n", avgBytes, kbps);

    const std::vector<int16_t> silence(size_t(kSilenceFrames) * kFrameSamples * kChannels, 0);
    size_t tailBytes = 0;
    const int kTailFrames = 10;
    for (int f = 0; f < kSilenceFrames; ++f) {
        const int16_t* in = silence.data() + size_t(f) * kFrameSamples * kChannels;
        const int written = opus_encode(enc, in, kFrameSamples, packet.data(), kMaxPacketBytes);
        if (written < 0) {
            std::printf("FAIL: opus_encode on silent frame %d returned %s\n", f,
                opus_strerror(written));
            return 1;
        }
        if (f >= kSilenceFrames - kTailFrames) tailBytes += size_t(written);
    }
    const double silentAvg = double(tailBytes) / kTailFrames;
    if (silentAvg <= 3.0) {
        std::printf("PASS: DTX collapsed silence to %.1f bytes per frame\n", silentAvg);
    } else {
        std::printf("INFO: DTX did not engage, silence costs %.1f bytes per frame\n", silentAvg);
    }

    opus_encoder_destroy(enc);
    opus_decoder_destroy(dec);
    std::printf("M0 SMOKE TEST PASSED\n");
    return 0;
}
