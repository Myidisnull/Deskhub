#include "deskhub/media/PcmRing.h"
#include "deskhubp/audio/AudioSink.h"
#include "deskhubp/diag/Log.h"

#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>

#if TARGET_OS_IPHONE
#include <AVFoundation/AVFoundation.h>
#endif

namespace deskhubp {

namespace {

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

    deskhub::media::PcmRing ring;

    static OSStatus Render(void* userData, AudioUnitRenderActionFlags* flags,
        const AudioTimeStamp* time, UInt32 bus, UInt32 frames, AudioBufferList* data);
};

OSStatus AudioSink::Impl::Render(void* userData, AudioUnitRenderActionFlags* flags,
    const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* data) {
    auto* impl = static_cast<AudioSink::Impl*>(userData);
    if (data->mNumberBuffers == 0) return noErr;

    auto* out = static_cast<int16_t*>(data->mBuffers[0].mData);
    const size_t wanted = size_t(frames) * impl->format.channels;
    if (impl->ring.TakeOrSilence(out, wanted) == 0 && flags != nullptr)
        *flags |= kAudioUnitRenderAction_OutputIsSilence;
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
    impl->ring.Open(format);

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
    return "coreaudio";
}

}
