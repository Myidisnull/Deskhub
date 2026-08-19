#include "deskhubp/audio/AudioSink.h"
#include "deskhubp/diag/Log.h"

#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace deskhubp {

namespace {

constexpr size_t kRingFrames = 8;
constexpr REFERENCE_TIME kBufferDuration = 400'000;
constexpr DWORD kRenderWaitMs = 200;

struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
    bool owned() const {
        return SUCCEEDED(hr);
    }
};

WAVEFORMATEX MakeWaveFormat(const deskhub::media::AudioFormat& format) {
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = WORD(format.channels);
    wf.nSamplesPerSec = format.sampleRate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = WORD(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    wf.cbSize = 0;
    return wf;
}

}

struct AudioSink::Impl {
    deskhub::media::AudioFormat format{};

    mutable std::mutex ringMutex;
    std::vector<int16_t> ring;
    size_t readAt = 0;
    size_t filled = 0;

    std::thread thread;
    std::atomic<bool> quit{false};
    std::atomic<uint64_t> dropped{0};
    std::atomic<uint64_t> starved{0};
    std::atomic<bool> opened{false};

    HANDLE bufferReady = nullptr;
    HANDLE ready = nullptr;

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

    void Run();
};

void AudioSink::Impl::Run() {
    ComScope com;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator));
    Microsoft::WRL::ComPtr<IMMDevice> device;
    if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);

    Microsoft::WRL::ComPtr<IAudioClient> client;
    if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);

    const WAVEFORMATEX wf = MakeWaveFormat(format);
    if (SUCCEEDED(hr))
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            kBufferDuration, 0, &wf, nullptr);
    if (SUCCEEDED(hr)) hr = client->SetEventHandle(bufferReady);

    UINT32 bufferFrames = 0;
    if (SUCCEEDED(hr)) hr = client->GetBufferSize(&bufferFrames);

    Microsoft::WRL::ComPtr<IAudioRenderClient> render;
    if (SUCCEEDED(hr)) hr = client->GetService(IID_PPV_ARGS(&render));
    if (SUCCEEDED(hr)) hr = client->Start();

    if (FAILED(hr)) {
        LOGE("[audio] evt=sink_open_fail backend=wasapi hr=0x%08lx", static_cast<unsigned long>(hr));
        SetEvent(ready);
        return;
    }

    opened.store(true, std::memory_order_release);
    SetEvent(ready);

    while (!quit.load(std::memory_order_acquire)) {
        if (WaitForSingleObject(bufferReady, kRenderWaitMs) != WAIT_OBJECT_0) continue;

        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding))) break;
        const UINT32 free = bufferFrames - padding;
        if (!free) continue;

        BYTE* raw = nullptr;
        if (FAILED(render->GetBuffer(free, &raw))) break;

        auto* out = reinterpret_cast<int16_t*>(raw);
        const size_t wanted = size_t(free) * format.channels;
        const size_t got = Take(out, wanted);
        if (got < wanted) {
            std::memset(out + got, 0, (wanted - got) * sizeof(int16_t));
            starved.fetch_add(1, std::memory_order_relaxed);
        }
        render->ReleaseBuffer(free, 0);
    }

    client->Stop();
}

AudioSink::AudioSink() = default;

AudioSink::~AudioSink() {
    Close();
}

bool AudioSink::Open(const deskhub::media::AudioFormat& format) {
    Close();
    if (!deskhub::media::IsSupportedAudioFormat(format)) return false;

    auto impl = std::make_unique<Impl>();
    impl->format = format;
    impl->ring.assign(kRingFrames * format.interleavedSamples(), 0);
    impl->bufferReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    impl->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (impl->bufferReady == nullptr || impl->ready == nullptr) {
        if (impl->bufferReady != nullptr) CloseHandle(impl->bufferReady);
        if (impl->ready != nullptr) CloseHandle(impl->ready);
        return false;
    }

    Impl* raw = impl.get();
    impl->thread = std::thread([raw] { raw->Run(); });
    WaitForSingleObject(impl->ready, INFINITE);

    if (!impl->opened.load(std::memory_order_acquire)) {
        impl->quit.store(true, std::memory_order_release);
        impl->thread.join();
        CloseHandle(impl->bufferReady);
        CloseHandle(impl->ready);
        return false;
    }

    impl_ = std::move(impl);
    LOGI("[audio] evt=sink_open backend=wasapi rate=%u ch=%u", format.sampleRate,
        format.channels);
    return true;
}

void AudioSink::Close() {
    if (!impl_) return;
    impl_->quit.store(true, std::memory_order_release);
    SetEvent(impl_->bufferReady);
    if (impl_->thread.joinable()) impl_->thread.join();
    CloseHandle(impl_->bufferReady);
    CloseHandle(impl_->ready);
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
    return "wasapi";
}

}
