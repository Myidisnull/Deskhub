#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "decode/MfDecoder.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <codecapi.h>
#include <icodecapi.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>

#include "deskhubp/diag/Log.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")

using Microsoft::WRL::ComPtr;

#define MFD_CHECK(expr, msg)                              \
    do {                                                  \
        HRESULT _hr = (expr);                             \
        if (FAILED(_hr)) {                                \
            LOGE("[MfDecoder] %s failed: 0x%08lX", (msg), \
                (unsigned long)_hr);                      \
            return false;                                 \
        }                                                 \
    } while (0)

struct MfDecoder::Impl {
    ComPtr<IMFTransform> mft;
    ComPtr<IMFDXGIDeviceManager> deviceManager;
    FrameHandler onFrame;
    DecoderConfig cfg{};
    UINT resetToken = 0;
    bool mfStarted = false;
    bool streaming = false;
    uint32_t outWidth = 0, outHeight = 0;
    uint64_t lastInputTsUs = 0;
    bool tsMismatchLogged = false;
    uint64_t framesOut = 0;

    ~Impl() {
        if (mft && streaming) {
            mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        }
        mft.Reset();
        deviceManager.Reset();
        if (mfStarted) MFShutdown();
    }

    GUID SubtypeFor(Codec c) const {
        return (c == Codec::HEVC) ? MFVideoFormat_HEVC : MFVideoFormat_H264;
    }

    bool Init(ID3D11Device* device, const DecoderConfig& c, FrameHandler handler) {
        cfg = c;
        onFrame = std::move(handler);

        ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&mt)))) {
            mt->SetMultithreadProtected(TRUE);
        }

        MFD_CHECK(MFStartup(MF_VERSION, MFSTARTUP_LITE), "MFStartup");
        mfStarted = true;

        MFT_REGISTER_TYPE_INFO inInfo{MFMediaType_Video, SubtypeFor(cfg.codec)};
        MFT_REGISTER_TYPE_INFO outInfo{MFMediaType_Video, MFVideoFormat_NV12};
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        MFD_CHECK(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                      MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                      &inInfo, &outInfo, &activates, &count),
            "MFTEnumEx");
        if (count == 0) {
            LOGE("[MfDecoder] No decoder MFT found.");
            return false;
        }
        HRESULT hr = activates[0]->ActivateObject(IID_PPV_ARGS(&mft));
        for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
        CoTaskMemFree(activates);
        MFD_CHECK(hr, "ActivateObject");

        ComPtr<IMFAttributes> attrs;
        if (SUCCEEDED(mft->GetAttributes(&attrs))) {
            UINT32 aware = 0;
            attrs->GetUINT32(MF_SA_D3D11_AWARE, &aware);
            if (!aware) {
                LOGE("[MfDecoder] MFT does not support D3D11 - unusable.");
                return false;
            }
            attrs->SetUINT32(MF_LOW_LATENCY, TRUE);
        }
        RequestLowLatencyMode();
        MFD_CHECK(MFCreateDXGIDeviceManager(&resetToken, &deviceManager),
            "MFCreateDXGIDeviceManager");
        MFD_CHECK(deviceManager->ResetDevice(device, resetToken), "ResetDevice");
        MFD_CHECK(mft->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                      (ULONG_PTR)deviceManager.Get()),
            "SET_D3D_MANAGER");

        ComPtr<IMFMediaType> inType;
        MFD_CHECK(MFCreateMediaType(&inType), "MFCreateMediaType(in)");
        inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        inType->SetGUID(MF_MT_SUBTYPE, SubtypeFor(cfg.codec));
        if (cfg.width && cfg.height) {
            MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, cfg.width, cfg.height);
        }
        MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, cfg.fps ? cfg.fps : 60, 1);
        inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_MixedInterlaceOrProgressive);
        MFD_CHECK(mft->SetInputType(0, inType.Get(), 0), "SetInputType");

        if (!NegotiateOutputType()) return false;

        MFD_CHECK(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0),
            "NOTIFY_BEGIN_STREAMING");
        MFD_CHECK(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0),
            "NOTIFY_START_OF_STREAM");
        streaming = true;

        LOGI("[MfDecoder] Initialized: %s, D3D11VA, low-latency.",
            cfg.codec == Codec::HEVC ? "HEVC" : "H264");
        return true;
    }

    void RequestLowLatencyMode() {
        ComPtr<ICodecAPI> codec;
        if (FAILED(mft.As(&codec))) {
            LOGW("[MfDecoder] No ICodecAPI - stream reorder delay stays at the DPB default.");
            return;
        }
        VARIANT on;
        VariantInit(&on);
        on.vt = VT_BOOL;
        on.boolVal = VARIANT_TRUE;
        const HRESULT hr = codec->SetValue(&CODECAPI_AVLowLatencyMode, &on);
        VariantClear(&on);
        if (FAILED(hr))
            LOGW(
                "[MfDecoder] AVLowLatencyMode rejected: 0x%08lX - frames may be held for"
                " reordering.",
                (unsigned long)hr);
    }

    bool NegotiateOutputType() {
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> t;
            HRESULT hr = mft->GetOutputAvailableType(0, i, &t);
            if (hr == MF_E_NO_MORE_TYPES) break;
            MFD_CHECK(hr, "GetOutputAvailableType");
            GUID sub{};
            t->GetGUID(MF_MT_SUBTYPE, &sub);
            if (sub == MFVideoFormat_NV12) {
                MFD_CHECK(mft->SetOutputType(0, t.Get(), 0), "SetOutputType");
                MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &outWidth, &outHeight);
                MFVideoArea area{};
                if ((SUCCEEDED(t->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,
                         (UINT8*)&area, sizeof(area), nullptr)) ||
                        SUCCEEDED(t->GetBlob(MF_MT_GEOMETRIC_APERTURE,
                            (UINT8*)&area, sizeof(area), nullptr))) &&
                    area.Area.cx > 0 && area.Area.cy > 0 &&
                    uint32_t(area.Area.cx) <= outWidth &&
                    uint32_t(area.Area.cy) <= outHeight) {
                    outWidth = uint32_t(area.Area.cx);
                    outHeight = uint32_t(area.Area.cy);
                } else if (cfg.width && cfg.height &&
                           cfg.width <= outWidth && outWidth - cfg.width < 16 &&
                           cfg.height <= outHeight && outHeight - cfg.height < 16) {
                    outWidth = cfg.width;
                    outHeight = cfg.height;
                }
                const uint32_t range = MFGetAttributeUINT32(t.Get(),
                    MF_MT_VIDEO_NOMINAL_RANGE, 0);
                const uint32_t matrix = MFGetAttributeUINT32(t.Get(), MF_MT_YUV_MATRIX, 0);
                LOGI("[MfDecoder] Output %ux%u, nominalRange=%u, yuvMatrix=%u",
                    outWidth, outHeight, range, matrix);
                return true;
            }
        }
        LOGE("[MfDecoder] MFT does not offer NV12.");
        return false;
    }

    bool Decode(const uint8_t* data, size_t size, uint64_t timestampUs) {
        if (!streaming || !data || size == 0) return false;

        ComPtr<IMFMediaBuffer> buffer;
        MFD_CHECK(MFCreateMemoryBuffer((DWORD)size, &buffer), "MFCreateMemoryBuffer");
        BYTE* dst = nullptr;
        MFD_CHECK(buffer->Lock(&dst, nullptr, nullptr), "Lock");
        std::memcpy(dst, data, size);
        buffer->Unlock();
        buffer->SetCurrentLength((DWORD)size);

        ComPtr<IMFSample> sample;
        MFD_CHECK(MFCreateSample(&sample), "MFCreateSample");
        MFD_CHECK(sample->AddBuffer(buffer.Get()), "AddBuffer");
        sample->SetSampleTime((LONGLONG)(timestampUs * 10ull));
        sample->SetSampleDuration(10'000'000ll / (cfg.fps ? cfg.fps : 60));
        lastInputTsUs = timestampUs;

        HRESULT hr = mft->ProcessInput(0, sample.Get(), 0);
        if (hr == MF_E_NOTACCEPTING) {
            if (!DrainOutputs()) return false;
            hr = mft->ProcessInput(0, sample.Get(), 0);
        }
        MFD_CHECK(hr, "ProcessInput");

        return DrainOutputs();
    }

    bool DrainOutputs() {
        for (;;) {
            MFT_OUTPUT_STREAM_INFO si{};
            MFD_CHECK(mft->GetOutputStreamInfo(0, &si), "GetOutputStreamInfo");
            const bool mftProvides = (si.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                                       MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            if (!mftProvides) {
                LOGE("[MfDecoder] MFT does not provide D3D samples - unsupported.");
                return false;
            }

            MFT_OUTPUT_DATA_BUFFER ob{};
            ob.dwStreamID = 0;
            DWORD status = 0;
            HRESULT hr = mft->ProcessOutput(0, 1, &ob, &status);
            if (ob.pEvents) {
                ob.pEvents->Release();
                ob.pEvents = nullptr;
            }

            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return true;
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                if (!NegotiateOutputType()) return false;
                continue;
            }
            if (FAILED(hr)) {
                if (ob.pSample) ob.pSample->Release();
                LOGE("[MfDecoder] ProcessOutput failed: 0x%08lX", (unsigned long)hr);
                return false;
            }

            ComPtr<IMFSample> outSample;
            outSample.Attach(ob.pSample);
            if (outSample && !Deliver(outSample.Get())) return false;
        }
    }

    bool Deliver(IMFSample* sample) {
        ComPtr<IMFMediaBuffer> buffer;
        MFD_CHECK(sample->GetBufferByIndex(0, &buffer), "GetBufferByIndex");
        ComPtr<IMFDXGIBuffer> dxgiBuf;
        MFD_CHECK(buffer.As(&dxgiBuf), "IMFDXGIBuffer");

        ComPtr<ID3D11Texture2D> tex;
        MFD_CHECK(dxgiBuf->GetResource(IID_PPV_ARGS(&tex)), "GetResource");
        UINT subresource = 0;
        dxgiBuf->GetSubresourceIndex(&subresource);

        LONGLONG timeHns = 0;
        sample->GetSampleTime(&timeHns);
        const uint64_t mftTsUs = (uint64_t)timeHns / 10ull;
        if (!tsMismatchLogged && lastInputTsUs && mftTsUs) {
            const uint64_t diff =
                mftTsUs > lastInputTsUs ? mftTsUs - lastInputTsUs : lastInputTsUs - mftTsUs;
            if (diff > 100'000) {
                tsMismatchLogged = true;
                LOGI(
                    "[DIAG] evt=mft_ts_drift input_us=%llu mft_us=%llu diff_ms=%llu",
                    (unsigned long long)lastInputTsUs, (unsigned long long)mftTsUs,
                    (unsigned long long)(diff / 1000));
            }
        }

        DecodedFrame df{};
        df.texture = tex.Get();
        df.subresource = subresource;
        df.width = outWidth;
        df.height = outHeight;
        df.timestampUs = lastInputTsUs ? lastInputTsUs : mftTsUs;
        ++framesOut;
        if (onFrame) onFrame(df);
        return true;
    }
};

MfDecoder::MfDecoder() = default;
MfDecoder::~MfDecoder() = default;

bool MfDecoder::Init(ID3D11Device* device, const DecoderConfig& cfg, FrameHandler onFrame) {
    impl_ = std::make_unique<Impl>();
    if (!impl_->Init(device, cfg, std::move(onFrame))) {
        impl_.reset();
        return false;
    }
    return true;
}

bool MfDecoder::Decode(const uint8_t* data, size_t size, uint64_t timestampUs) {
    return impl_ && impl_->Decode(data, size, timestampUs);
}

std::unique_ptr<IVideoDecoder> CreateDecoder(ID3D11Device* device, const DecoderConfig& cfg,
    IVideoDecoder::FrameHandler onFrame) {
    auto dec = std::make_unique<MfDecoder>();
    if (dec->Init(device, cfg, std::move(onFrame))) {
        LOGI("[Decoder] Using backend: %s", dec->BackendName());
        return dec;
    }
    LOGE("[Decoder] Failed to initialize any backend.");
    return nullptr;
}
