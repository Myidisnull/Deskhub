#include "deskhubp/audio/AudioSink.h"
#include "deskhubp/diag/Log.h"

#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>

#if TARGET_OS_IPHONE
#include <AVFoundation/AVFoundation.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace deskhubp {

namespace {

constexpr size_t kRingFrames = 8;

#if TARGET_OS_IPHONE
constexpr OSType kOutputSubType = kAudioUnitSubType_RemoteIO;
#else
constexpr OSType kOutputSubType = kAudioUnitSubType_DefaultOutput;
#endif

constexpr AudioUnitElement kOutputBus = 0;

#if TARGET_OS_IPHONE
bool PrepareSession() {
    NSError* error = nil;
    AVAudioSession* session = [AVAudioSession sharedInstance];
    if (![session setCategory:AVAudioSessionCategoryPlayback
                        mode:AVAudioSessionModeMoviePlayback
                     options:AVAudioSessionCategoryOptionMixWithOthers
                       error:&error]) {
        LOGE("[audio] evt=sink_session_fail err=%s", error.localizedDescription.UTF8String);
        return false;
    }
    if (![session setActive:YES error:&error]) {
        LOGE("[audio] evt=sink_session_activate_fail err=%s",
            error.localizedDescription.UTF8String);
        return false;
    }
    return true;
}
#endif

}

struct AudioSink::Impl {
    AudioUnit unit = nullptr;
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

    static OSStatus Render(void* userData, AudioUnitRenderActionFlags* flags,
        const AudioTimeStamp* time, UInt32 bus, UInt32 frames, AudioBufferList* data);
};

OSStatus AudioSink::Impl::Render(void* userData, AudioUnitRenderActionFlags* flags,
    const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* data) {
    auto* impl = static_cast<AudioSink::Impl*>(userData);
    if (data->mNumberBuffers == 0) return noErr;

    auto* out = static_cast<int16_t*>(data->mBuffers[0].mData);
    const size_t wanted = size_t(frames) * impl->format.channels;
    const size_t got = impl->Take(out, wanted);
    if (got < wanted) {
        std::memset(out + got, 0, (wanted - got) * sizeof(int16_t));
        impl->starved.fetch_add(1, std::memory_order_relaxed);
        if (got == 0 && flags != nullptr) *flags |= kAudioUnitRenderAction_OutputIsSilence;
    }
    data->mBuffers[0].mDataByteSize = UInt32(wanted * sizeof(int16_t));
    return noErr;
}

AudioSink::AudioSink() = default;

AudioSink::~AudioSink() {
    Close();
}

bool AudioSink::Open(const deskhub::media::AudioFormat& format) {
    Close();
    if (!deskhub::media::IsSupportedAudioFormat(format)) return false;

#if TARGET_OS_IPHONE
    if (!PrepareSession()) return false;
#endif

    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kOutputSubType;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    if (component == nullptr) {
        LOGE("[audio] evt=sink_open_fail backend=coreaudio step=component");
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->format = format;
    impl->ring.assign(kRingFrames * format.interleavedSamples(), 0);

    if (AudioComponentInstanceNew(component, &impl->unit) != noErr || impl->unit == nullptr) {
        LOGE("[audio] evt=sink_open_fail backend=coreaudio step=instance");
        return false;
    }

    AudioStreamBasicDescription asbd{};
    asbd.mSampleRate = double(format.sampleRate);
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    asbd.mFramesPerPacket = 1;
    asbd.mChannelsPerFrame = format.channels;
    asbd.mBitsPerChannel = 16;
    asbd.mBytesPerFrame = UInt32(format.channels * sizeof(int16_t));
    asbd.mBytesPerPacket = asbd.mBytesPerFrame;

    OSStatus status = AudioUnitSetProperty(impl->unit, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input, kOutputBus, &asbd, sizeof(asbd));

    AURenderCallbackStruct callback{};
    callback.inputProc = Impl::Render;
    callback.inputProcRefCon = impl.get();
    if (status == noErr)
        status = AudioUnitSetProperty(impl->unit, kAudioUnitProperty_SetRenderCallback,
            kAudioUnitScope_Input, kOutputBus, &callback, sizeof(callback));

    if (status == noErr) status = AudioUnitInitialize(impl->unit);
    if (status == noErr) status = AudioOutputUnitStart(impl->unit);

    if (status != noErr) {
        LOGE("[audio] evt=sink_open_fail backend=coreaudio status=%d", int(status));
        AudioComponentInstanceDispose(impl->unit);
        return false;
    }

    impl_ = std::move(impl);
    LOGI("[audio] evt=sink_open backend=coreaudio rate=%u ch=%u", format.sampleRate,
        format.channels);
    return true;
}

void AudioSink::Close() {
    if (!impl_) return;
    if (impl_->unit != nullptr) {
        AudioOutputUnitStop(impl_->unit);
        AudioUnitUninitialize(impl_->unit);
        AudioComponentInstanceDispose(impl_->unit);
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
    return "coreaudio";
}

}
