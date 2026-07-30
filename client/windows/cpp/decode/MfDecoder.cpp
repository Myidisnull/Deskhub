// =============================================================================
// MfDecoder.cpp — cài đặt đường giải mã Media Foundation.
//
// BỐ CỤC
//   Init()                — dựng MFT, gắn D3D device, đặt kiểu vào/ra.
//   NegotiateOutputType() — chọn NV12 trong danh sách MFT đề nghị.
//   Decode()              — nạp một NAL vào MFT rồi vét output.
//   DrainOutputs()        — rút mọi frame đã giải xong.
//   Deliver()             — bóc texture + subresource ra, gọi callback.
//
// THỨ TỰ ĐẶT KIỂU NGƯỢC VỚI ENCODER
//   Ở đây SetInputType đi TRƯỚC, rồi mới thương lượng đầu ra. Hợp lý: decoder phải
//   biết nó đang giải mã cái gì rồi mới nói được nó xuất ra được những định dạng
//   nào. Bên MfEncoder thì ngược lại (xem ghi chú ở đó).
//
// MF_E_TRANSFORM_STREAM_CHANGE LÀ ĐƯỜNG CHẠY BÌNH THƯỜNG, KHÔNG PHẢI LỖI
//   Kích thước thật của video nằm trong SPS, mà SPS chỉ đến cùng dữ liệu. Nên MFT
//   hay báo STREAM_CHANGE sau vài frame đầu để nói "kích thước thật khác cái anh
//   khai lúc Init". Ta chỉ việc thương lượng lại kiểu đầu ra rồi chạy tiếp.
//   Đây cũng là cách host đổi độ phân giải giữa chừng (RECONFIG) mà client không
//   phải dựng lại decoder — khác hẳn bản Android, nơi MediaCodec buộc phải dựng lại.
//
// ⚠ QUY TẮC SỞ HỮU SAMPLE ĐẦU RA
//   MFT tự cấp sample (mftProvides == true) và trao cho ta MỘT tham chiếu. Ta phải
//   tự nhả — đó là việc của ComPtr::Attach trong DrainOutputs. Quên thì rò VRAM,
//   và pool của decoder cạn sau vài trăm frame.
//
// LIÊN QUAN: decode/MfDecoder.h (vì sao chọn MF), decode/IVideoDecoder.h
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "decode/MfDecoder.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

using Microsoft::WRL::ComPtr;

#define MFD_CHECK(expr, msg)                                       \
    do {                                                           \
        HRESULT _hr = (expr);                                      \
        if (FAILED(_hr)) {                                         \
            std::printf("[MfDecoder] %s failed: 0x%08lX\n", (msg), \
                (unsigned long)_hr);                               \
            return false;                                          \
        }                                                          \
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
    // Mốc thời gian của frame VỪA NẠP VÀO. Đây là đồng hồ host thật, đi thẳng từ
    // Reassembler xuống; mốc MFT trả ra ở output có thể là số nó tự dựng theo
    // SampleDuration (xem Deliver).
    uint64_t lastInputTsUs = 0;
    bool tsMismatchLogged = false; // evt=mft_ts_drift in đúng một lần
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

        // BẮT BUỘC, cùng lý do như MfEncoder::Init: immediate context của D3D11 mặc
        // định không an toàn đa luồng, mà MFT chạy việc trên thread riêng còn ta gọi
        // từ thread Decode. Thiếu là hỏng ngẫu nhiên, rất khó tái hiện.
        ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&mt)))) {
            mt->SetMultithreadProtected(TRUE);
        }

        MFD_CHECK(MFStartup(MF_VERSION, MFSTARTUP_LITE), "MFStartup");
        mfStarted = true;

        // Tìm decoder MFT ĐỒNG BỘ (MFT async của vendor cần event-loop riêng, không cần
        // thiết: MFT của Microsoft vẫn decode hardware qua D3D11VA khi có device manager).
        MFT_REGISTER_TYPE_INFO inInfo{MFMediaType_Video, SubtypeFor(cfg.codec)};
        MFT_REGISTER_TYPE_INFO outInfo{MFMediaType_Video, MFVideoFormat_NV12};
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        MFD_CHECK(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                      MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                      &inInfo, &outInfo, &activates, &count),
            "MFTEnumEx");
        if (count == 0) {
            std::printf("[MfDecoder] No decoder MFT found.\n");
            return false;
        }
        HRESULT hr = activates[0]->ActivateObject(IID_PPV_ARGS(&mft));
        for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
        CoTaskMemFree(activates);
        MFD_CHECK(hr, "ActivateObject");

        // Gán D3D11 device -> decode hardware, output NV12 nằm ngay trong VRAM.
        ComPtr<IMFAttributes> attrs;
        if (SUCCEEDED(mft->GetAttributes(&attrs))) {
            UINT32 aware = 0;
            attrs->GetUINT32(MF_SA_D3D11_AWARE, &aware);
            if (!aware) {
                std::printf("[MfDecoder] MFT does not support D3D11 - unusable.\n");
                return false;
            }
            attrs->SetUINT32(MF_LOW_LATENCY, TRUE); // trả frame ngay, không giữ buffer
        }
        MFD_CHECK(MFCreateDXGIDeviceManager(&resetToken, &deviceManager),
            "MFCreateDXGIDeviceManager");
        MFD_CHECK(deviceManager->ResetDevice(device, resetToken), "ResetDevice");
        MFD_CHECK(mft->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                      (ULONG_PTR)deviceManager.Get()),
            "SET_D3D_MANAGER");

        // Kiểu đầu vào: H.264/HEVC elementary stream (Annex-B).
        ComPtr<IMFMediaType> inType;
        MFD_CHECK(MFCreateMediaType(&inType), "MFCreateMediaType(in)");
        inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        inType->SetGUID(MF_MT_SUBTYPE, SubtypeFor(cfg.codec));
        if (cfg.width && cfg.height) {
            MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, cfg.width, cfg.height);
        }
        MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, cfg.fps ? cfg.fps : 60, 1);
        // MixedInterlaceOrProgressive chứ không khai cứng Progressive: để MFT tự đọc
        // từ SPS. Khai cứng mà stream khác đi thì nó từ chối kiểu đầu vào.
        inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_MixedInterlaceOrProgressive);
        MFD_CHECK(mft->SetInputType(0, inType.Get(), 0), "SetInputType");

        if (!NegotiateOutputType()) return false;

        MFD_CHECK(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0),
            "NOTIFY_BEGIN_STREAMING");
        MFD_CHECK(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0),
            "NOTIFY_START_OF_STREAM");
        streaming = true;

        std::printf("[MfDecoder] Initialized: %s, D3D11VA, low-latency.\n",
            cfg.codec == Codec::HEVC ? "HEVC" : "H264");
        return true;
    }

    // Chọn output NV12 trong danh sách MFT đề nghị. Gọi lại mỗi khi stream đổi
    // (MF_E_TRANSFORM_STREAM_CHANGE - ví dụ SPS báo kích thước khác).
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
                // MF_MT_FRAME_SIZE có thể là kích thước CODED (bội 16 của H.264,
                // vd. 1124x634 -> 1136x640), không phải kích thước hiển thị. Lấy
                // theo aperture nếu MFT khai — thiếu bước cắt này thì phần đệm
                // (chroma 0 = xanh lá) hiện thành dải ở mép phải/dưới cửa sổ.
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
                    // MFT không khai aperture: lùi về kích thước phiên đã đàm phán,
                    // nhưng CHỈ khi chênh lệch đúng cỡ phần đệm alignment — RECONFIG
                    // đổi hẳn độ phân giải thì không được lấy số cũ đè lên.
                    outWidth = cfg.width;
                    outHeight = cfg.height;
                }
                // Chẩn đoán màu: range/matrix mà decoder ĐỌC ĐƯỢC từ bitstream (VUI).
                // 2 = MFNominalRange_16_235, 1 = 0_255; matrix 3 = BT709, 2 = BT601.
                const uint32_t range = MFGetAttributeUINT32(t.Get(),
                    MF_MT_VIDEO_NOMINAL_RANGE, 0);
                const uint32_t matrix = MFGetAttributeUINT32(t.Get(), MF_MT_YUV_MATRIX, 0);
                std::printf("[MfDecoder] Output %ux%u, nominalRange=%u, yuvMatrix=%u\n",
                    outWidth, outHeight, range, matrix);
                return true;
            }
        }
        std::printf("[MfDecoder] MFT does not offer NV12.\n");
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
        sample->SetSampleTime((LONGLONG)(timestampUs * 10ull)); // 100ns
        // ⚠ SampleDuration LÀ MỘT LỜI NÓI DỐI KHI NGUỒN TĨNH, và đó là chuyện có
        //   thật: host chỉ phát keepalive ~2 frame/giây, nhưng ta vẫn khai mỗi mẫu
        //   dài 1/fps (~16 ms ở 60fps). Ta vẫn khai vì MFT low-latency muốn có
        //   duration, nhưng KHÔNG được để con số đó quay lại làm timestamp — xem
        //   `lastInputTsUs` ở Deliver.
        sample->SetSampleDuration(10'000'000ll / (cfg.fps ? cfg.fps : 60));
        // Nhớ mốc ĐẦU VÀO. Đây mới là đồng hồ host thật; cái đọc ra từ MFT có thể
        // là số nó tự dựng.
        lastInputTsUs = timestampUs;

        HRESULT hr = mft->ProcessInput(0, sample.Get(), 0);
        if (hr == MF_E_NOTACCEPTING) {
            // MFT đầy output chưa ai lấy -> rút sạch rồi thử lại đúng một lần.
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
                // Đường CPU (không D3D) không hỗ trợ ở GD2 - cần VRAM cho renderer.
                std::printf("[MfDecoder] MFT does not provide D3D samples - unsupported.\n");
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
                std::printf("[MfDecoder] ProcessOutput failed: 0x%08lX\n", (unsigned long)hr);
                return false;
            }

            // Attach (không AddRef) vì MFT đã trao ta quyền sở hữu tham chiếu đó.
            // ComPtr sẽ Release khi ra khỏi vòng — xem quy tắc sở hữu ở đầu file.
            ComPtr<IMFSample> outSample;
            outSample.Attach(ob.pSample);
            if (outSample && !Deliver(outSample.Get())) return false;
        }
    }

    // Bóc texture NV12 + subresource ra khỏi sample rồi gọi callback.
    //
    // GetSubresourceIndex là mấu chốt: texture trả về là một TEXTURE-ARRAY dùng
    // chung, frame này nằm ở lát nào thì chỉ số đó nói. Xem cảnh báo ở
    // IVideoDecoder.h — bỏ qua nó là vẽ nhầm frame.
    bool Deliver(IMFSample* sample) {
        ComPtr<IMFMediaBuffer> buffer;
        MFD_CHECK(sample->GetBufferByIndex(0, &buffer), "GetBufferByIndex");
        ComPtr<IMFDXGIBuffer> dxgiBuf;
        MFD_CHECK(buffer.As(&dxgiBuf), "IMFDXGIBuffer");

        ComPtr<ID3D11Texture2D> tex;
        MFD_CHECK(dxgiBuf->GetResource(IID_PPV_ARGS(&tex)), "GetResource");
        UINT subresource = 0;
        dxgiBuf->GetSubresourceIndex(&subresource);

        // ⚠ DÙNG MỐC ĐẦU VÀO, KHÔNG PHẢI MỐC MFT TRẢ RA (sửa 30/07/2026).
        //   Bản trước lấy sample->GetSampleTime() của output. Nghe hợp lý — nhưng nó
        //   giả định MFT chép nguyên mốc từ input sang output, mà đó là hành vi của
        //   DRIVER, không phải hợp đồng. Khi nguồn tĩnh (host phát keepalive ~2fps)
        //   trong khi ta khai SampleDuration = 1/60s, một MFT tự dựng mốc output theo
        //   duration sẽ cho PTS bò lên 16 ms mỗi frame trong khi đồng hồ thật đi 500 ms
        //   — tức PTS TỤT LẠI ~950 ms mỗi giây, TÍCH LUỸ.
        //   Đúng triệu chứng đo được 30/07: màn hình đứng yên thì e2e leo đều tới 5,7
        //   GIÂY rồi sập về ~40 ms ngay khi có hoạt động; và CHỈ client Windows bị,
        //   vì VtDecoder (Apple) dùng thẳng `ptsUs` truyền vào, không hỏi lại decoder.
        //   Ta vốn đã BIẾT mốc đúng — nó là tham số của Decode(). Đừng đi hỏi lại
        //   một bên có thể trả lời khác.
        //
        //   Một lời gọi Decode có thể sinh nhiều output, nên về lý thuyết mốc này chỉ
        //   đúng cho cái cuối. Chuỗi của ta không có B-frame và MF_LOW_LATENCY đang
        //   bật nên thực tế là một-đổi-một; và kể cả khi không, mốc đầu vào GẦN NHẤT
        //   vẫn sát hơn nhiều so với một đồng hồ tổng hợp trôi mãi.
        LONGLONG timeHns = 0;
        sample->GetSampleTime(&timeHns);
        const uint64_t mftTsUs = (uint64_t)timeHns / 10ull;
        // Đo, không đoán: nếu MFT thực sự chép nguyên mốc thì dòng này không bao giờ
        // in ra. In MỘT LẦN mỗi decoder — đây là chẩn đoán, không phải đường nóng.
        if (!tsMismatchLogged && lastInputTsUs && mftTsUs) {
            const uint64_t diff =
                mftTsUs > lastInputTsUs ? mftTsUs - lastInputTsUs : lastInputTsUs - mftTsUs;
            if (diff > 100'000) { // >100ms: quá lớn để là làm tròn
                tsMismatchLogged = true;
                std::printf(
                    "[DIAG] evt=mft_ts_drift input_us=%llu mft_us=%llu diff_ms=%llu\n",
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

// Factory (backend duy nhất hiện tại). Sang GD3 nếu cần NVDEC thì thêm vào đây.
std::unique_ptr<IVideoDecoder> CreateDecoder(ID3D11Device* device, const DecoderConfig& cfg,
    IVideoDecoder::FrameHandler onFrame) {
    auto dec = std::make_unique<MfDecoder>();
    if (dec->Init(device, cfg, std::move(onFrame))) {
        std::printf("[Decoder] Using backend: %ls\n", dec->BackendName());
        return dec;
    }
    std::printf("[Decoder] Failed to initialize any backend.\n");
    return nullptr;
}
