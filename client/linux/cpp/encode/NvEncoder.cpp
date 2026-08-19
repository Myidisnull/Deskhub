#include "encode/NvEncoder.h"

#include <drm_fourcc.h>

#include <ffnvcodec/dynlink_cuda.h>
#include <ffnvcodec/nvEncodeAPI.h>

#include <ffnvcodec/dynlink_loader.h>

#include <cstring>
#include <span>
#include <vector>

#include "deskhub/media/H264Sps.h"
#include "deskhub/media/RatePlan.h"
#include "deskhub/media/RgbDownscale.h"
#include "deskhubp/diag/Log.h"

using deskhub::media::EncoderConfig;

namespace {

NV_ENC_BUFFER_FORMAT NvencFormatFor(uint32_t drmFormat) {
    switch (drmFormat) {
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_ARGB8888: return NV_ENC_BUFFER_FORMAT_ARGB;
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_ABGR8888: return NV_ENC_BUFFER_FORMAT_ABGR;
        default: return NV_ENC_BUFFER_FORMAT_UNDEFINED;
    }
}

uint32_t DriverNvencVersion(NvencFunctions* nv) {
    uint32_t version = 0;
    if (nv->NvEncodeAPIGetMaxSupportedVersion(&version) != NV_ENC_SUCCESS) return 0;
    return version;
}

constexpr uint32_t kHeaderNvencVersion = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;

}

struct NvEncoder::Impl {
    CudaFunctions* cu = nullptr;
    NvencFunctions* nv = nullptr;
    NV_ENCODE_API_FUNCTION_LIST fn{};
    CUcontext cuCtx = nullptr;
    void* session = nullptr;
    NV_ENC_INPUT_PTR inputBuf = nullptr;
    NV_ENC_OUTPUT_PTR bitstream = nullptr;

    NV_ENC_INITIALIZE_PARAMS init{};
    NV_ENC_CONFIG encCfg{};

    deskhub::media::RgbDownscaler scaler;
    NV_ENC_BUFFER_FORMAT bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;

    EncoderConfig cfg{};
    std::vector<uint8_t> lastScaled;
    bool haveSource = false;

    const char* LastApiError() {
        const char* msg =
            session && fn.nvEncGetLastErrorString ? fn.nvEncGetLastErrorString(session) : nullptr;
        return msg ? msg : "no detail";
    }

    bool PushCtx() {
        return cu->cuCtxPushCurrent(cuCtx) == CUDA_SUCCESS;
    }

    void PopCtx() {
        CUcontext dummy = nullptr;
        cu->cuCtxPopCurrent(&dummy);
    }

    void ApplyRateControl(uint32_t bitrateBps) {
        const deskhub::media::RatePlan plan =
            deskhub::media::PlanRateControl(bitrateBps, cfg.fps, cfg.lowLatency);
        encCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
        encCfg.rcParams.averageBitRate = bitrateBps;
        encCfg.rcParams.maxBitRate = bitrateBps;
        encCfg.rcParams.vbvBufferSize = plan.vbvBits;
        encCfg.rcParams.vbvInitialDelay = plan.vbvInitialBits;
    }

    bool EncodeScaled(uint64_t timestampUs, bool forceKeyframe) {
        NV_ENC_LOCK_INPUT_BUFFER lock{};
        lock.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
        lock.inputBuffer = inputBuf;
        if (fn.nvEncLockInputBuffer(session, &lock) != NV_ENC_SUCCESS) {
            LOGE("[NvEnc] input buffer lock failed: %s", LastApiError());
            return false;
        }
        const uint32_t rowBytes = cfg.width * deskhub::media::kPackedPixelBytes;
        auto* dst = static_cast<uint8_t*>(lock.bufferDataPtr);
        for (uint32_t y = 0; y < cfg.height; ++y)
            std::memcpy(dst + size_t(y) * lock.pitch, lastScaled.data() + size_t(y) * rowBytes,
                rowBytes);
        const uint32_t pitch = lock.pitch;
        fn.nvEncUnlockInputBuffer(session, inputBuf);

        NV_ENC_PIC_PARAMS pic{};
        pic.version = NV_ENC_PIC_PARAMS_VER;
        pic.inputWidth = cfg.width;
        pic.inputHeight = cfg.height;
        pic.inputPitch = pitch;
        pic.inputBuffer = inputBuf;
        pic.outputBitstream = bitstream;
        pic.bufferFmt = bufferFormat;
        pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        pic.inputTimeStamp = timestampUs;
        if (forceKeyframe)
            pic.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
        const NVENCSTATUS encoded = fn.nvEncEncodePicture(session, &pic);
        if (encoded == NV_ENC_ERR_NEED_MORE_INPUT) return true;
        if (encoded != NV_ENC_SUCCESS) {
            LOGE("[NvEnc] encode failed: %s", LastApiError());
            return false;
        }

        NV_ENC_LOCK_BITSTREAM out{};
        out.version = NV_ENC_LOCK_BITSTREAM_VER;
        out.outputBitstream = bitstream;
        if (fn.nvEncLockBitstream(session, &out) != NV_ENC_SUCCESS) {
            LOGE("[NvEnc] bitstream lock failed: %s", LastApiError());
            return false;
        }
        const bool keyframe = out.pictureType == NV_ENC_PIC_TYPE_IDR;
        const auto* data = static_cast<const uint8_t*>(out.bitstreamBufferPtr);
        size_t size = out.bitstreamSizeInBytes;
        std::vector<uint8_t> zeroReorder;
        if (keyframe && size) {
            zeroReorder = deskhub::media::AnnexBStreamWithZeroReorder(
                std::span<const uint8_t>(data, size));
            if (!zeroReorder.empty()) {
                data = zeroReorder.data();
                size = zeroReorder.size();
            }
        }
        if (cfg.onPacket && size) cfg.onPacket(data, size, timestampUs, keyframe);
        fn.nvEncUnlockBitstream(session, bitstream);
        return true;
    }

    void Shutdown() {
        if (session) {
            NV_ENC_PIC_PARAMS eos{};
            eos.version = NV_ENC_PIC_PARAMS_VER;
            eos.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
            fn.nvEncEncodePicture(session, &eos);
            if (inputBuf) fn.nvEncDestroyInputBuffer(session, inputBuf);
            if (bitstream) fn.nvEncDestroyBitstreamBuffer(session, bitstream);
            fn.nvEncDestroyEncoder(session);
            session = nullptr;
            inputBuf = nullptr;
            bitstream = nullptr;
        }
        if (cuCtx) {
            cu->cuCtxDestroy(cuCtx);
            cuCtx = nullptr;
        }
        if (nv) nvenc_free_functions(&nv);
        if (cu) cuda_free_functions(&cu);
        haveSource = false;
        lastScaled.clear();
    }
};

NvEncoder::NvEncoder() : impl_(std::make_unique<Impl>()) {}

NvEncoder::~NvEncoder() {
    impl_->Shutdown();
}

bool NvEncoder::DriverPresent() {
    static const bool present = [] {
        CudaFunctions* cu = nullptr;
        NvencFunctions* nv = nullptr;
        if (cuda_load_functions(&cu, nullptr) < 0) return false;
        if (nvenc_load_functions(&nv, nullptr) < 0) {
            cuda_free_functions(&cu);
            return false;
        }
        const uint32_t driver = DriverNvencVersion(nv);
        nvenc_free_functions(&nv);
        cuda_free_functions(&cu);
        if (driver < kHeaderNvencVersion) {
            LOGI("[NvEnc] driver speaks NVENC API %u.%u but %u.%u is needed — not using it.",
                driver >> 4, driver & 0xF, NVENCAPI_MAJOR_VERSION, NVENCAPI_MINOR_VERSION);
            return false;
        }
        return true;
    }();
    return present;
}

bool NvEncoder::Init(const EncoderConfig& cfg, uint32_t drmFormat) {
    Impl* im = impl_.get();
    im->cfg = cfg;
    im->bufferFormat = NvencFormatFor(drmFormat);
    if (im->bufferFormat == NV_ENC_BUFFER_FORMAT_UNDEFINED) {
        LOGE("[NvEnc] unsupported capture pixel format 0x%08x.", drmFormat);
        return false;
    }

    if (cuda_load_functions(&im->cu, nullptr) < 0) return false;
    if (nvenc_load_functions(&im->nv, nullptr) < 0) return false;
    if (DriverNvencVersion(im->nv) < kHeaderNvencVersion) return false;

    if (im->cu->cuInit(0) != CUDA_SUCCESS) return false;
    int deviceCount = 0;
    if (im->cu->cuDeviceGetCount(&deviceCount) != CUDA_SUCCESS || deviceCount < 1) return false;
    CUdevice device = 0;
    if (im->cu->cuDeviceGet(&device, 0) != CUDA_SUCCESS) return false;
    if (im->cu->cuCtxCreate(&im->cuCtx, 0, device) != CUDA_SUCCESS) return false;

    im->fn.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    if (im->nv->NvEncodeAPICreateInstance(&im->fn) != NV_ENC_SUCCESS) return false;

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};
    open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    open.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    open.device = im->cuCtx;
    open.apiVersion = NVENCAPI_VERSION;
    if (im->fn.nvEncOpenEncodeSessionEx(&open, &im->session) != NV_ENC_SUCCESS) {
        im->session = nullptr;
        return false;
    }

    NV_ENC_PRESET_CONFIG preset{};
    preset.version = NV_ENC_PRESET_CONFIG_VER;
    preset.presetCfg.version = NV_ENC_CONFIG_VER;
    if (im->fn.nvEncGetEncodePresetConfigEx(im->session, NV_ENC_CODEC_H264_GUID,
            NV_ENC_PRESET_P4_GUID, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY,
            &preset) != NV_ENC_SUCCESS) {
        LOGE("[NvEnc] preset config query failed: %s", im->LastApiError());
        return false;
    }
    im->encCfg = preset.presetCfg;
    im->encCfg.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
    im->encCfg.gopLength = NVENC_INFINITE_GOPLENGTH;
    im->encCfg.frameIntervalP = 1;
    im->encCfg.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;
    im->encCfg.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
    im->ApplyRateControl(cfg.bitrateBps);

    im->init.version = NV_ENC_INITIALIZE_PARAMS_VER;
    im->init.encodeGUID = NV_ENC_CODEC_H264_GUID;
    im->init.presetGUID = NV_ENC_PRESET_P4_GUID;
    im->init.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
    im->init.encodeWidth = cfg.width;
    im->init.encodeHeight = cfg.height;
    im->init.darWidth = cfg.width;
    im->init.darHeight = cfg.height;
    im->init.maxEncodeWidth = cfg.width;
    im->init.maxEncodeHeight = cfg.height;
    im->init.frameRateNum = cfg.fps ? cfg.fps : 60;
    im->init.frameRateDen = 1;
    im->init.enablePTD = 1;
    im->init.encodeConfig = &im->encCfg;

    if (!im->PushCtx()) return false;
    const NVENCSTATUS st = im->fn.nvEncInitializeEncoder(im->session, &im->init);
    im->PopCtx();
    if (st != NV_ENC_SUCCESS) {
        LOGE("[NvEnc] encoder init %ux%u @%u fps refused: %s", cfg.width, cfg.height, cfg.fps,
            im->LastApiError());
        return false;
    }

    NV_ENC_CREATE_INPUT_BUFFER in{};
    in.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
    in.width = cfg.width;
    in.height = cfg.height;
    in.bufferFmt = im->bufferFormat;
    if (im->fn.nvEncCreateInputBuffer(im->session, &in) != NV_ENC_SUCCESS) {
        LOGE("[NvEnc] input buffer creation failed: %s", im->LastApiError());
        return false;
    }
    im->inputBuf = in.inputBuffer;

    NV_ENC_CREATE_BITSTREAM_BUFFER out{};
    out.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    if (im->fn.nvEncCreateBitstreamBuffer(im->session, &out) != NV_ENC_SUCCESS) {
        LOGE("[NvEnc] bitstream buffer creation failed: %s", im->LastApiError());
        return false;
    }
    im->bitstream = out.bitstreamBuffer;

    im->lastScaled.assign(size_t(cfg.width) * cfg.height * deskhub::media::kPackedPixelBytes, 0);
    im->haveSource = false;
    LOGI("[NvEnc] %ux%u @%u fps, %.1f Mbps CBR, ultra-low-latency — NVIDIA hardware encoder.",
        cfg.width, cfg.height, cfg.fps, cfg.bitrateBps / 1e6);
    return true;
}

bool NvEncoder::Encode(const LinuxFrameInfo& fi, uint64_t timestampUs, bool forceKeyframe) {
    Impl* im = impl_.get();
    if (!IsOpen()) return false;
    if (fi.memory != FrameMemory::Mapped) return false;

    if (NvencFormatFor(fi.drmFormat) != im->bufferFormat) {
        LOGE("[NvEnc] capture pixel format 0x%08x no longer matches the input buffer.",
            fi.drmFormat);
        return false;
    }

    if (!im->scaler.Matches(fi.meta.width, fi.meta.height, im->cfg.width, im->cfg.height))
        im->scaler.Configure(fi.meta.width, fi.meta.height, im->cfg.width, im->cfg.height);
    if (!im->scaler.ready()) return false;

    im->scaler.Scale(fi.handle, fi.stride, im->lastScaled.data(),
        im->cfg.width * deskhub::media::kPackedPixelBytes);
    im->haveSource = true;

    return im->EncodeScaled(timestampUs, forceKeyframe);
}

bool NvEncoder::EncodeLast(uint64_t timestampUs, bool forceKeyframe) {
    Impl* im = impl_.get();
    if (!IsOpen() || !im->haveSource) return false;
    return im->EncodeScaled(timestampUs, forceKeyframe);
}

bool NvEncoder::haveSourceFrame() const {
    return impl_->haveSource;
}

bool NvEncoder::SetBitrate(uint32_t bitrateBps) {
    Impl* im = impl_.get();
    if (!IsOpen() || !bitrateBps || bitrateBps == im->cfg.bitrateBps) return IsOpen();
    im->cfg.bitrateBps = bitrateBps;
    im->ApplyRateControl(bitrateBps);

    NV_ENC_RECONFIGURE_PARAMS re{};
    re.version = NV_ENC_RECONFIGURE_PARAMS_VER;
    re.reInitEncodeParams = im->init;
    if (im->fn.nvEncReconfigureEncoder(im->session, &re) != NV_ENC_SUCCESS) {
        LOGW("[NvEnc] bitrate reconfigure to %u bps failed: %s", bitrateBps, im->LastApiError());
        return false;
    }
    return true;
}

void NvEncoder::Finish() {
    impl_->Shutdown();
}

bool NvEncoder::IsOpen() const {
    return impl_->session != nullptr;
}
