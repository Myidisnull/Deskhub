// =============================================================================
// VaEncoder.cpp — cài đặt bộ mã hoá H.264 trên VA-API.
//
// BỐ CỤC
//   Tiện ích: bảng DRM fourcc ↔ VA fourcc, tra level_idc, VaCheck.
//   BuildParameterSets()  — dựng byte SPS/PPS bằng BitWriter.
//   CreateContexts()      — config/context/surface cho encode + VPP.
//   ImportDmaBuf/UploadMapped — đưa frame nguồn thành một VA surface RGB.
//   ConvertToNv12()       — VPP đổi màu.
//   EncodeNv12()          — nộp frame + đọc bitstream.
//
// ⚠ MÔ HÌNH THAM CHIẾU — CHỖ DỄ SAI NHẤT
//   VA-API phân biệt BA surface trong một lần nộp, và trộn chúng là lỗi im lặng
//   (hình vỡ dần chứ không báo lỗi):
//     - surface NGUỒN: truyền cho vaBeginPicture(). Đây là ảnh ta muốn nén.
//     - surface TÁI DỰNG: pic_param.CurrPic.picture_id. Driver ghi ảnh đã giải
//       nén lại vào đây để làm tham chiếu cho frame sau.
//     - surface THAM CHIẾU: pic_param.ReferenceFrames[] + slice.RefPicList0[].
//       Chính là surface tái dựng của một frame TRƯỚC.
//   Ta giữ một srcNv12_ (đầu ra VPP) và hai reconNv12_ luân phiên: frame chẵn tái
//   dựng vào [0] và tham chiếu [1], frame lẻ thì ngược lại.
//
// ⚠ GOP VÔ HẠN LÀ CỦA TA, KHÔNG PHẢI CỦA DRIVER
//   intra_period = intra_idr_period = 0 nghĩa là driver KHÔNG tự chèn I/IDR.
//   Toàn bộ quyết định phát IDR nằm ở AgentLoop (cờ forceIdr, do client xin qua
//   REQUEST_KEYFRAME). Lý do ở docs/06 §5: IDR lớn gấp nhiều lần P-frame, phát nó
//   theo lịch cố định là đổ dầu vào lửa đúng lúc đường truyền đang nghẽn.
//
// LIÊN QUAN: encode/VaEncoder.h (lý do thiết kế), encode/BitWriter.h,
//            encode/VaDisplay.h
// =============================================================================
#include "encode/VaEncoder.h"

#include <va/va_drmcommon.h>

#include <drm_fourcc.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "deskhubp/Log.h"
#include "deskhubp/Clock.h"
#include "encode/BitWriter.h"
#include "encode/VaDisplay.h"

namespace {

// Bao nhiêu byte tối thiểu cho bộ đệm bitstream của MỘT frame. IDR ở 4K có thể
// vài trăm KB; cấp theo w*h*3/2 là cỡ một frame NV12 chưa nén — không frame nén
// nào vượt được mức đó.
constexpr size_t kMinCodedBufSize = 1 << 20;

bool VaCheck(VAStatus st, const char* what) {
    if (st == VA_STATUS_SUCCESS) return true;
    LOGE("[VaEnc] %s failed: %s", what, vaErrorStr(st));
    return false;
}

// DRM fourcc (ScreenCapture nói) → VA fourcc (VA-API nói). Hai bên gọi tên ngược
// nhau vì DRM đọc theo giá trị 32-bit little-endian còn VA đọc theo thứ tự byte —
// xem chú thích cùng nội dung ở capture/ScreenCapture.cpp.
uint32_t DrmToVaFourcc(uint32_t drm) {
    switch (drm) {
        case DRM_FORMAT_XRGB8888: return VA_FOURCC_BGRX;
        case DRM_FORMAT_ARGB8888: return VA_FOURCC_BGRA;
        case DRM_FORMAT_XBGR8888: return VA_FOURCC_RGBX;
        case DRM_FORMAT_ABGR8888: return VA_FOURCC_RGBA;
        default: return 0;
    }
}

// Level tối thiểu chứa nổi độ phân giải + tốc độ khung này (bảng A-1 của H.264).
// Khai level thấp hơn thực tế thì bộ giải mã nghiêm ngặt sẽ từ chối stream.
uint8_t LevelFor(uint32_t mbW, uint32_t mbH, uint32_t fps) {
    const uint64_t frameMbs = uint64_t(mbW) * mbH;
    const uint64_t mbps = frameMbs * (fps ? fps : 60);
    struct Level {
        uint8_t idc;
        uint64_t maxMbps;
        uint64_t maxFrameMbs;
    };
    static const Level kLevels[] = {
        {30, 40500, 1620},      // 3.0  — 720x576@25
        {31, 108000, 3600},     // 3.1  — 1280x720@30
        {32, 216000, 5120},     // 3.2  — 1280x720@60
        {40, 245760, 8192},     // 4.0  — 1920x1080@30
        {42, 522240, 8704},     // 4.2  — 1920x1080@60
        {50, 589824, 22080},    // 5.0
        {51, 983040, 36864},    // 5.1  — 4096x2160@30
        {52, 2073600, 36864},   // 5.2  — 4096x2160@60
        {60, 4177920, 139264},  // 6.0  — 8K
        {61, 8355840, 139264},  // 6.1
        {62, 16711680, 139264}, // 6.2
    };
    for (const Level& l : kLevels)
        if (mbps <= l.maxMbps && frameMbs <= l.maxFrameMbs) return l.idc;
    return 62;
}

// Cấp một VAEncMiscParameterBuffer và trả về con trỏ tới payload để người gọi
// điền. VA-API bắt buộc dùng khuôn này: header {type} rồi payload ngay sau.
template <typename T>
VABufferID MakeMiscBuffer(VADisplay dpy, VAContextID ctx, VAEncMiscParameterType type, T** payload) {
    VABufferID id = VA_INVALID_ID;
    if (vaCreateBuffer(dpy, ctx, VAEncMiscParameterBufferType,
            sizeof(VAEncMiscParameterBuffer) + sizeof(T), 1, nullptr, &id) != VA_STATUS_SUCCESS)
        return VA_INVALID_ID;
    VAEncMiscParameterBuffer* hdr = nullptr;
    if (vaMapBuffer(dpy, id, reinterpret_cast<void**>(&hdr)) != VA_STATUS_SUCCESS) {
        vaDestroyBuffer(dpy, id);
        return VA_INVALID_ID;
    }
    hdr->type = type;
    *payload = reinterpret_cast<T*>(hdr->data);
    std::memset(*payload, 0, sizeof(T));
    return id;
}

VAPictureH264 InvalidPic() {
    VAPictureH264 p{};
    p.picture_id = VA_INVALID_SURFACE;
    p.flags = VA_PICTURE_H264_INVALID;
    return p;
}

// --- ĐIỀU TIẾT BITRATE KHI CHỈ CÓ CQP ---------------------------------------
//
// VÌ SAO PHẢI TỰ LÀM
//   Entrypoint LP của iHD chỉ nhận VA_RC_CQP (docs/17 §3). CQP nghĩa là "QP cố
//   định": driver phát ra bao nhiêu bit là hệ quả của QP, và cfg_.bitrateBps
//   không có đường nào tác động vào đó. Hậu quả đo được trên máy thật: mục tiêu
//   20 Mbps ra ~35 Mbps, và khi BitrateController hạ mục tiêu xuống 1 Mbps để
//   chữa nghẽn thì luồng vẫn phát ~14 Mbps — congestion control mất hoàn toàn
//   tác dụng, mất gói không giảm, client xin IDR mỗi 250ms, mỗi IDR ~360 KB lại
//   nạp thêm vào đúng chỗ đang nghẽn. Vòng xoáy đó chỉ cắt được từ gốc: làm cho
//   bitrate mục tiêu THẬT SỰ điều khiển kích thước frame.
//
// ⚠ HAI CÁCH LÀM SAI ĐÃ THỬ VÀ ĐO — ĐỪNG QUAY LẠI
//   (1) So kích thước TỪNG frame với hạn mức rồi nhích QP theo mức lệch. Dao động
//       ngay: nội dung desktop chênh cả chục lần giữa hai frame liền nhau (con trỏ
//       nhích một ô ≈ 2 KB, kéo cửa sổ ≈ 50 KB), nên bộ điều khiển đuổi theo NHIỄU
//       chứ không theo tốc độ bit. Quỹ đạo đo được: QP 26→28→24→28→24→21→25 với
//       kích thước 12 KB→1.9 KB→18 KB→1.9 KB. Tệ hơn: bitrate đo được KHÔNG đơn
//       điệu theo mục tiêu (mục tiêu 20 Mbps ra ÍT bit hơn mục tiêu 5 Mbps).
//   (2) Cộng dồn lệch vào một bộ đệm ảo rồi CỘNG THÊM vào QP mỗi frame theo độ đầy.
//       Windup kinh điển: độ đầy ĐÃ LÀ tích phân của sai lệch, cộng dồn nó lần nữa
//       là tích phân hai lần. Đo được QP leo 27→28→…→45 và ở đó, trong khi frame
//       chỉ còn 354 B trên hạn mức 10 417 B — nợ của một IDR mất mười mấy frame mới
//       trả xong, mà mỗi frame đó lại +1 QP.
//
// CÁCH DÙNG Ở ĐÂY
//   Lọc TÍN HIỆU rồi mới tích phân MỘT lần. Giữ trung bình trượt của
//   ln(kích thước / hạn mức) trên các P-frame; mỗi frame nhích QP nhiều nhất ±1 theo
//   trung bình đó. Log domain vì kích thước ~ 2^(-QP/6): mỗi +1 QP giảm ~12%, nên
//   ln(tỉ lệ)/ln(1.125) đổi trực tiếp "lệch bao nhiêu lần" thành "lệch bao nhiêu QP".
//   Gần cân bằng thì trung bình trượt về 0, bước làm tròn về 0, QP đứng yên.
constexpr int kQpMin = 16; // dưới mức này gần như vô nghĩa với nội dung desktop
constexpr int kQpMax = 45; // trên mức này hình nhoè tới mức không dùng được
// Bước tối đa mỗi frame. ±1 là CỐ Ý: tín hiệu đã được lọc, còn đây là khâu tích
// phân — cho nó nhảy nhiều chính là lỗi (1) ở trên.
constexpr int kQpStepMax = 1;
// IDR nén thô hơn P-frame một chút. Ngược với quy ước phát video (ở đó keyframe
// được ưu tiên nét), và có lý do: một IDR là một BURST, và burst vượt sức đường
// truyền chính là thứ đã gây chuỗi mất gói → client xin IDR → burst nữa. Dự án ưu
// tiên độ trễ hơn độ nét của keyframe (docs/06 §5), nên đánh đổi theo chiều này.
constexpr int kIdrQpDelta = 2;
// Hằng số thời gian của trung bình trượt, tính bằng số frame ở fps danh nghĩa.
// ~0.25s: đủ dài để lọc nhiễu nội dung, đủ ngắn để không thành độ trễ điều khiển.
constexpr double kEmaFrames = 15.0;
// ⚠ IDR CÓ HẠN MỨC RIÊNG, TÍNH THEO GIÂY
//   Chỉ để IDR thừa hưởng qp_ của P-frame là không đủ, và đây là chỗ đo được: trên
//   desktop tĩnh, vòng P đúng khi hạ QP xuống sàn (P-frame còn thừa hạn mức), nhưng
//   IDR thừa hưởng QP thấp đó nên vẫn ~220 KB — và trên toàn dải mục tiêu 1..20 Mbps
//   nó chỉ co từ 224 KB xuống 149 KB, tức 1.5 lần cho 20 lần mục tiêu. Trên màn hình
//   ít thay đổi, IDR chiếm quá nửa tổng số byte, nên chính nó quyết định bitrate —
//   và chính nó là cú burst đã gây vòng xoáy mất gói trong log.
//   Hạn mức tính theo THỜI GIAN (bao nhiêu giây bitrate mục tiêu) chứ không theo số
//   frame: cùng một lý lẽ độ trễ với hrd->buffer_size, và nó co giãn đúng theo mục
//   tiêu — đó là tính chất còn thiếu.
constexpr double kIdrSeconds = 0.25;
// Trần dịch chuyển QP giữa hai IDR liên tiếp. IDR thưa nên mỗi cái là một quan sát
// đáng tin, sửa thẳng tới đích được; vẫn chặn để một IDR dị thường không kéo cái
// sau đi quá xa.
constexpr int kIdrQpJump = 6;
// Trần thời gian được tính hạn mức cho MỘT frame. Sau một quãng màn hình đứng im,
// elapsed có thể là vài giây; cấp hạn mức theo cả quãng đó sẽ đẩy QP xuống sát
// kQpMin rồi frame động đầu tiên phình ra đúng lúc không nên.
constexpr double kMaxBudgetFrames = 4.0;

} // namespace

VaEncoder::~VaEncoder() {
    Finish();
}

// ---------------------------------------------------------------------------
// SPS / PPS — xem "vì sao phải tự viết" ở VaEncoder.h
// ---------------------------------------------------------------------------
void VaEncoder::BuildParameterSets() {
    const uint8_t level = LevelFor(mbW_, mbH_, cfg_.fps);

    // CropUnitX/Y = 2 với chroma_format_idc = 1 và frame_mbs_only_flag = 1, nên
    // offset tính bằng ĐƠN VỊ 2 MẪU LUMA chứ không phải pixel.
    const uint32_t cropRight = (alignedW_ - cfg_.width) / 2;
    const uint32_t cropBottom = (alignedH_ - cfg_.height) / 2;
    const bool crop = cropRight || cropBottom;

    BitWriter w;
    w.StartNal(3, 7); // nal_ref_idc = 3, nal_unit_type = 7 (SPS)
    w.U(8, 77);       // profile_idc = Main. Đủ cho CABAC; không cần các trường
                      // phụ của High (transform 8x8, scaling list).
    w.U(8, 0);        // constraint_set0..5 + reserved_zero_2bits
    w.U(8, level);
    w.UE(0); // seq_parameter_set_id
    w.UE(kLog2MaxFrameNumMinus4);
    w.UE(0); // pic_order_cnt_type = 0
    w.UE(kLog2MaxPocLsbMinus4);
    w.UE(kMaxRefFrames);
    w.U(1, 0); // gaps_in_frame_num_value_allowed_flag
    w.UE(mbW_ - 1);
    w.UE(mbH_ - 1); // frame_mbs_only_flag = 1 nên map units = MB rows
    w.U(1, 1);      // frame_mbs_only_flag
    w.U(1, 1);      // direct_8x8_inference_flag
    w.U(1, crop ? 1 : 0);
    if (crop) {
        w.UE(0);
        w.UE(cropRight);
        w.UE(0);
        w.UE(cropBottom);
    }
    w.U(1, 1); // vui_parameters_present_flag
    {
        w.U(1, 0); // aspect_ratio_info_present_flag
        w.U(1, 0); // overscan_info_present_flag
        // KHAI BÁO KHÔNG GIAN MÀU TƯỜNG MINH. Thiếu nó thì bộ giải mã tự đoán
        // (thường BT.601 cho SD, BT.709 cho HD) trong khi VPP của ta đổi màu theo
        // BT.709 cố định — lệch nhau cho ra hình ám xanh/đỏ nhẹ, kiểu lỗi rất dễ
        // đổ oan cho màn hình.
        w.U(1, 1); // video_signal_type_present_flag
        w.U(3, 5); // video_format = 5 (unspecified)
        w.U(1, 0); // video_full_range_flag = 0 (limited range, 16-235)
        w.U(1, 1); // colour_description_present_flag
        w.U(8, 1); // colour_primaries = BT.709
        w.U(8, 1); // transfer_characteristics = BT.709
        w.U(8, 1); // matrix_coefficients = BT.709
        w.U(1, 0); // chroma_loc_info_present_flag
        w.U(1, 1); // timing_info_present_flag
        w.U(32, 1);
        w.U(32, 2 * (cfg_.fps ? cfg_.fps : 60)); // time_scale = 2*fps (đơn vị field)
        w.U(1, 1);                               // fixed_frame_rate_flag
        w.U(1, 0);                               // nal_hrd_parameters_present_flag
        w.U(1, 0);                               // vcl_hrd_parameters_present_flag
        w.U(1, 0);                               // pic_struct_present_flag
        w.U(1, 0);                               // bitstream_restriction_flag
    }
    w.Trailing();
    sps_ = w.bytes();
    spsBits_ = w.bitLength();

    w.Clear();
    w.StartNal(3, 8); // PPS
    w.UE(0);          // pic_parameter_set_id
    w.UE(0);          // seq_parameter_set_id
    w.U(1, 1);        // entropy_coding_mode_flag = CABAC
    w.U(1, 0);        // bottom_field_pic_order_in_frame_present_flag
    w.UE(0);          // num_slice_groups_minus1
    w.UE(0);          // num_ref_idx_l0_default_active_minus1
    w.UE(0);          // num_ref_idx_l1_default_active_minus1
    w.U(1, 0);        // weighted_pred_flag
    w.U(2, 0);        // weighted_bipred_idc
    w.SE(0);          // pic_init_qp_minus26 -> pic_init_qp = 26
    w.SE(0);          // pic_init_qs_minus26
    w.SE(0);          // chroma_qp_index_offset
    w.U(1, 1);        // deblocking_filter_control_present_flag
    w.U(1, 0);        // constrained_intra_pred_flag
    w.U(1, 0);        // redundant_pic_cnt_present_flag
    w.Trailing();
    pps_ = w.bytes();
    ppsBits_ = w.bitLength();
}

// ---------------------------------------------------------------------------
// Init / CreateContexts
// ---------------------------------------------------------------------------
bool VaEncoder::Init(const EncoderConfig& cfg) {
    Finish();

    if (!cfg.width || !cfg.height || (cfg.width & 1) || (cfg.height & 1)) {
        LOGE("[VaEnc] Refusing %ux%u — dimensions must be even and non-zero.", cfg.width,
            cfg.height);
        return false;
    }
    VaDisplay& vd = VaDisplay::Instance();
    if (!vd.Open()) return false;

    cfg_ = cfg;
    dpy_ = vd.handle();
    encEntrypoint_ = vd.encodeEntrypoint();
    mbW_ = (cfg_.width + 15) / 16;
    mbH_ = (cfg_.height + 15) / 16;
    alignedW_ = mbW_ * 16;
    alignedH_ = mbH_ * 16;

    frameCount_ = 0;
    frameNum_ = 0;
    poc_ = 0;
    idrPicId_ = 0;
    haveRef_ = false;
    refSurface_ = VA_INVALID_SURFACE;
    pendingBitrate_ = 0;
    haveSource_ = false;
    qp_ = kPicInitQp;
    lastIdrQp_ = 0;
    lastIdrBytes_ = 0;
    logRatioEma_ = 0.0;
    haveRatio_ = false;
    lastEncodeUs_ = 0;

    BuildParameterSets();
    if (!CreateContexts()) {
        Finish();
        return false;
    }

    LOGI("[VaEnc] %ux%u (aligned %ux%u) @%u fps, %.1f Mbps — %s, packed headers %s.", cfg_.width,
        cfg_.height, alignedW_, alignedH_, cfg_.fps, cfg_.bitrateBps / 1e6,
        vd.driverName().c_str(), packedHeaders_ ? "on" : "OFF (driver writes its own SPS/PPS)");
    return true;
}

bool VaEncoder::CreateContexts() {
    // --- Config mã hoá ---
    // Ba thuộc tính hỏi driver một lượt: định dạng surface, kiểu điều tiết bitrate,
    // và có nhận packed header của ta không.
    VAConfigAttrib attrs[3];
    attrs[0].type = VAConfigAttribRTFormat;
    attrs[1].type = VAConfigAttribRateControl;
    attrs[2].type = VAConfigAttribEncPackedHeaders;
    if (!VaCheck(vaGetConfigAttributes(dpy_, VAProfileH264Main, encEntrypoint_, attrs, 3),
            "vaGetConfigAttributes"))
        return false;

    if (!(attrs[0].value & VA_RT_FORMAT_YUV420)) {
        LOGE("[VaEnc] Driver does not support YUV420 encode surfaces.");
        return false;
    }
    // CBR là thứ giao thức cần: BitrateController phía core điều tiết bằng cách
    // đặt bitrate mục tiêu, và nó giả định encoder bám sát con số đó. Driver nào
    // chỉ có CQP thì SetBitrate sẽ không có tác dụng — vẫn chạy, nhưng congestion
    // control mất răng, nên nói rõ trong log.
    //
    // ⚠ ĐÂY LÀ ĐƯỜNG BÌNH THƯỜNG, KHÔNG PHẢI CA HIẾM. Entrypoint LP của iHD chỉ
    // khai CQP (đo trên Rocket Lake / iHD 26.1.2: CBR, VBR, VCM đều bị từ chối),
    // nên mọi máy Intel Gen11+ rơi vào nhánh này. Đo thực tế: mục tiêu 20 Mbps ra
    // ~35 Mbps ở 1080p60 với nội dung động mạnh. Xem docs/17-linux-app.md §3.
    const bool haveCbr = (attrs[1].value != VA_ATTRIB_NOT_SUPPORTED) &&
                         (attrs[1].value & VA_RC_CBR);
    cqpMode_ = !haveCbr;
    if (cqpMode_)
        LOGI(
            "[VaEnc] Driver offers no CBR — using constant QP with software rate control "
            "(QP %d..%d).",
            kQpMin, kQpMax);

    packedHeaders_ = (attrs[2].value != VA_ATTRIB_NOT_SUPPORTED) &&
                     (attrs[2].value & VA_ENC_PACKED_HEADER_SEQUENCE) &&
                     (attrs[2].value & VA_ENC_PACKED_HEADER_PICTURE);

    VAConfigAttrib cfgAttrs[3];
    int nCfgAttrs = 0;
    cfgAttrs[nCfgAttrs].type = VAConfigAttribRTFormat;
    cfgAttrs[nCfgAttrs++].value = VA_RT_FORMAT_YUV420;
    cfgAttrs[nCfgAttrs].type = VAConfigAttribRateControl;
    cfgAttrs[nCfgAttrs++].value = haveCbr ? VA_RC_CBR : VA_RC_CQP;
    // CHỈ khai báo packed header khi driver đã nói là hỗ trợ: khai một giá trị nó
    // không nhận thì vaCreateConfig từ chối cả cấu hình, và ta mất luôn cả bộ mã
    // hoá chứ không riêng tính năng này.
    if (packedHeaders_) {
        cfgAttrs[nCfgAttrs].type = VAConfigAttribEncPackedHeaders;
        cfgAttrs[nCfgAttrs++].value =
            VA_ENC_PACKED_HEADER_SEQUENCE | VA_ENC_PACKED_HEADER_PICTURE;
    }

    if (!VaCheck(vaCreateConfig(dpy_, VAProfileH264Main, encEntrypoint_, cfgAttrs, nCfgAttrs,
                     &encConfig_),
            "vaCreateConfig(encode)"))
        return false;

    // --- Surface NV12: 1 nguồn + 2 tái dựng luân phiên ---
    VASurfaceID surfaces[3];
    if (!VaCheck(vaCreateSurfaces(dpy_, VA_RT_FORMAT_YUV420, alignedW_, alignedH_, surfaces, 3,
                     nullptr, 0),
            "vaCreateSurfaces(NV12)"))
        return false;
    srcNv12_ = surfaces[0];
    reconNv12_[0] = surfaces[1];
    reconNv12_[1] = surfaces[2];

    if (!VaCheck(vaCreateContext(dpy_, encConfig_, int(alignedW_), int(alignedH_),
                     VA_PROGRESSIVE, surfaces, 3, &encContext_),
            "vaCreateContext(encode)"))
        return false;

    const size_t codedSize =
        std::max(kMinCodedBufSize, size_t(alignedW_) * alignedH_ * 3 / 2);
    if (!VaCheck(vaCreateBuffer(dpy_, encContext_, VAEncCodedBufferType, uint32_t(codedSize), 1,
                     nullptr, &codedBuf_),
            "vaCreateBuffer(coded)"))
        return false;

    // --- Config + context VPP (đổi màu RGB → NV12, và CO nếu cỡ vào lớn hơn) ---
    // Context dựng theo cỡ RA (alignedW_/alignedH_): đó là hình học của surface đích.
    // Cỡ VÀO không cần khai ở đây — nó đi theo từng lời gọi qua surface_region, nên
    // cùng một context phục vụ được mọi cỡ nguồn (xem ConvertToNv12).
    if (!VaCheck(vaCreateConfig(dpy_, VAProfileNone, VAEntrypointVideoProc, nullptr, 0,
                     &vppConfig_),
            "vaCreateConfig(vpp)"))
        return false;
    if (!VaCheck(vaCreateContext(dpy_, vppConfig_, int(alignedW_), int(alignedH_), VA_PROGRESSIVE,
                     &srcNv12_, 1, &vppContext_),
            "vaCreateContext(vpp)"))
        return false;

    return true;
}

void VaEncoder::Finish() {
    if (!dpy_) return;

    if (haveRgbImage_) {
        vaDestroyImage(dpy_, rgbImage_.image_id);
        haveRgbImage_ = false;
    }
    if (rgbSurface_ != VA_INVALID_SURFACE) {
        vaDestroySurfaces(dpy_, &rgbSurface_, 1);
        rgbSurface_ = VA_INVALID_SURFACE;
    }
    if (codedBuf_ != VA_INVALID_ID) {
        vaDestroyBuffer(dpy_, codedBuf_);
        codedBuf_ = VA_INVALID_ID;
    }
    if (vppContext_ != VA_INVALID_ID) {
        vaDestroyContext(dpy_, vppContext_);
        vppContext_ = VA_INVALID_ID;
    }
    if (vppConfig_ != VA_INVALID_ID) {
        vaDestroyConfig(dpy_, vppConfig_);
        vppConfig_ = VA_INVALID_ID;
    }
    if (encContext_ != VA_INVALID_ID) {
        vaDestroyContext(dpy_, encContext_);
        encContext_ = VA_INVALID_ID;
    }
    if (encConfig_ != VA_INVALID_ID) {
        vaDestroyConfig(dpy_, encConfig_);
        encConfig_ = VA_INVALID_ID;
    }
    if (srcNv12_ != VA_INVALID_SURFACE) {
        VASurfaceID s[3] = {srcNv12_, reconNv12_[0], reconNv12_[1]};
        vaDestroySurfaces(dpy_, s, 3);
        srcNv12_ = reconNv12_[0] = reconNv12_[1] = VA_INVALID_SURFACE;
    }
    dpy_ = nullptr;
}

int VaEncoder::IdrQp() const {
    const double budget = double(cfg_.bitrateBps) / 8.0 * kIdrSeconds;
    int q = qp_ + kIdrQpDelta;
    if (lastIdrBytes_ > 0 && budget > 1.0) {
        // Một bước sửa thẳng tới đích: kích thước ~ 2^(-QP/6) nên từ (QP, byte) của
        // IDR trước suy ra ngay QP đáng lẽ phải dùng để vừa hạn mức. Không lọc trung
        // bình trượt như P-frame vì IDR thưa — đợi lọc là đợi mấy giây, đúng lúc
        // client đang xin IDR 4 lần/giây. Đây cũng là chỗ một lần hạ bitrate có tác
        // dụng NGAY ở IDR kế tiếp: budget đã tính theo mục tiêu mới.
        const int predicted = lastIdrQp_ +
                              int(std::lround(std::log(double(lastIdrBytes_) / budget) / std::log(1.125)));
        q = std::clamp(predicted, lastIdrQp_ - kIdrQpJump, lastIdrQp_ + kIdrQpJump);
    }
    // IDR không bao giờ mịn hơn P-frame: ưu tiên độ trễ hơn độ nét của keyframe.
    return std::clamp(std::max(q, qp_ + kIdrQpDelta), kQpMin, kQpMax);
}

bool VaEncoder::SetBitrate(uint32_t bitrateBps) {
    if (!IsOpen() || bitrateBps < 100'000) return false;
    // Ở chế độ CQP, nhích QP NGAY theo tỉ lệ mục tiêu vừa đổi thay vì đợi vòng đo
    // của EncodeNv12. Lý do là ca đã thấy trong log: khi nghẽn, BitrateController
    // hạ mục tiêu rất nhanh (20 → 15 → 11 → 8 Mbps trong vài giây) đúng lúc client
    // đang xin IDR mỗi 250ms. Đợi phản hồi từng frame nghĩa là mấy IDR đầu tiên vẫn
    // to như cũ — mà chính chúng là thứ đang gây nghẽn. Nhích trước thì frame kế
    // tiếp, kể cả IDR, đã ở QP mới.
    if (cqpMode_ && cfg_.bitrateBps && bitrateBps != cfg_.bitrateBps) {
        const int step = int(std::lround(
            std::log(double(cfg_.bitrateBps) / double(bitrateBps)) / std::log(1.125)));
        qp_ = std::clamp(qp_ + step, kQpMin, kQpMax);
        // Trung bình trượt cũ đo theo hạn mức CŨ; giữ lại là điều theo một mục tiêu
        // đã hết hiệu lực. Xoá để nó nạp lại từ hạn mức mới.
        logRatioEma_ = 0.0;
        haveRatio_ = false;
    }
    cfg_.bitrateBps = bitrateBps;
    pendingBitrate_ = bitrateBps; // áp ở lần nộp frame kế tiếp
    return true;
}

// ---------------------------------------------------------------------------
// Đưa frame nguồn thành một VA surface RGB
// ---------------------------------------------------------------------------

// Đường NHANH: bọc dma-buf của PipeWire thành VA surface, không chép byte nào.
//
// Surface tạo ra chỉ sống trong một frame và người gọi phải huỷ nó — dma-buf fd
// thuộc buffer của PipeWire và chỉ hợp lệ trong callback (CaptureTypes.h), nên
// giữ surface lại cho frame sau là trỏ vào bộ nhớ đã được tái sử dụng.
//
// ⚠ HAI ĐƯỜNG IMPORT, CHỌN THEO MODIFIER — ĐỪNG GỘP LÀM MỘT
//   DRM_PRIME_2 là đường import có modifier TƯỜNG MINH: ta nói cho driver layout
//   chính xác của buffer. DRM_FORMAT_MOD_INVALID KHÔNG phải một layout — nó là
//   giá trị lính canh của bước thoả thuận, nghĩa là "không nói, dùng layout ngầm
//   định". Nhồi nó vào drm_format_modifier là đưa driver một giá trị vô nghĩa, và
//   iHD từ chối thẳng (đo trên Rocket Lake: vaCreateSurfaces trả
//   VA_STATUS_ERROR_ALLOCATION_FAILED với MỌI độ phân giải). Hệ quả nhìn từ ngoài
//   rất khó lần: capture chạy 137 fps, encode hỏng 100%, và vì log chỉ báo MỘT
//   lần nên phần còn lại chỉ là enc_fail lặng lẽ.
//   Layout ngầm định phải đi bằng MEM_TYPE_DRM_PRIME (đường cũ) với
//   VASurfaceAttribExternalBuffers — struct đó KHÔNG có trường modifier, đúng
//   nghĩa "không nói".
VASurfaceID VaEncoder::ImportDmaBuf(const LinuxFrameInfo& fi) {
    const uint32_t vaFourcc = DrmToVaFourcc(fi.drmFormat);
    if (!vaFourcc || !fi.planeCount) return VA_INVALID_SURFACE;

    // Kích thước thật của mỗi dma-buf. Dùng lseek chứ không tính tay: công thức
    // (offset + height*stride) SAI với mọi layout có nén/tiling.
    uint32_t objSize[kMaxDmaPlanes]{};
    for (uint32_t i = 0; i < fi.planeCount; ++i) {
        const off_t sz = lseek(fi.planes[i].fd, 0, SEEK_END);
        objSize[i] = sz > 0 ? uint32_t(sz) : fi.planes[i].offset + fi.planes[i].stride * fi.height;
    }

    VASurfaceAttrib attrs[2]{};
    attrs[0].type = VASurfaceAttribMemoryType;
    attrs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrs[0].value.type = VAGenericValueTypeInteger;
    attrs[1].type = VASurfaceAttribExternalBufferDescriptor;
    attrs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrs[1].value.type = VAGenericValueTypePointer;

    // Cả hai descriptor khai ở đây để chúng sống tới lúc vaCreateSurfaces đọc.
    VADRMPRIMESurfaceDescriptor prime2{};
    VASurfaceAttribExternalBuffers legacy{};
    uintptr_t legacyFds[kMaxDmaPlanes]{};

    if (fi.modifier == DRM_FORMAT_MOD_INVALID) {
        legacy.pixel_format = vaFourcc;
        legacy.width = fi.width;
        legacy.height = fi.height;
        legacy.data_size = objSize[0];
        legacy.num_planes = fi.planeCount;
        for (uint32_t i = 0; i < fi.planeCount; ++i) {
            legacy.pitches[i] = fi.planes[i].stride;
            legacy.offsets[i] = fi.planes[i].offset;
            legacyFds[i] = uintptr_t(fi.planes[i].fd);
        }
        legacy.buffers = legacyFds;
        legacy.num_buffers = fi.planeCount;
        attrs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
        attrs[1].value.value.p = &legacy;
    } else {
        prime2.fourcc = vaFourcc;
        prime2.width = fi.width;
        prime2.height = fi.height;
        prime2.num_objects = fi.planeCount;
        for (uint32_t i = 0; i < fi.planeCount; ++i) {
            prime2.objects[i].fd = fi.planes[i].fd;
            prime2.objects[i].size = objSize[i];
            prime2.objects[i].drm_format_modifier = fi.modifier;
        }
        prime2.num_layers = 1;
        prime2.layers[0].drm_format = fi.drmFormat;
        prime2.layers[0].num_planes = fi.planeCount;
        for (uint32_t i = 0; i < fi.planeCount; ++i) {
            prime2.layers[0].object_index[i] = i;
            prime2.layers[0].offset[i] = fi.planes[i].offset;
            prime2.layers[0].pitch[i] = fi.planes[i].stride;
        }
        attrs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;
        attrs[1].value.value.p = &prime2;
    }

    VASurfaceID surf = VA_INVALID_SURFACE;
    const VAStatus st = vaCreateSurfaces(dpy_, VA_RT_FORMAT_RGB32, fi.width, fi.height, &surf, 1,
        attrs, 2);
    if (st != VA_STATUS_SUCCESS) {
        // Không log mỗi frame: driver không import được modifier này thì frame nào
        // cũng hỏng và log sẽ ngập. Encode() lo phần báo một lần.
        return VA_INVALID_SURFACE;
    }
    return surf;
}

// Đường LÙI: frame đã ở RAM, phải chép lên GPU. Surface + image được dùng lại
// giữa các frame, chỉ dựng lại khi đổi định dạng.
bool VaEncoder::UploadMapped(const LinuxFrameInfo& fi) {
    const uint32_t vaFourcc = DrmToVaFourcc(fi.drmFormat);
    if (!vaFourcc || !fi.data || !fi.stride) return false;

    if (rgbFourcc_ != vaFourcc || rgbSurface_ == VA_INVALID_SURFACE) {
        if (haveRgbImage_) {
            vaDestroyImage(dpy_, rgbImage_.image_id);
            haveRgbImage_ = false;
        }
        if (rgbSurface_ != VA_INVALID_SURFACE) {
            vaDestroySurfaces(dpy_, &rgbSurface_, 1);
            rgbSurface_ = VA_INVALID_SURFACE;
        }

        VASurfaceAttrib attr{};
        attr.type = VASurfaceAttribPixelFormat;
        attr.flags = VA_SURFACE_ATTRIB_SETTABLE;
        attr.value.type = VAGenericValueTypeInteger;
        attr.value.value.i = int(vaFourcc);
        if (!VaCheck(vaCreateSurfaces(dpy_, VA_RT_FORMAT_RGB32, fi.width, fi.height, &rgbSurface_,
                         1, &attr, 1),
                "vaCreateSurfaces(RGB)"))
            return false;

        VAImageFormat fmt{};
        fmt.fourcc = vaFourcc;
        fmt.byte_order = VA_LSB_FIRST;
        fmt.bits_per_pixel = 32;
        fmt.depth = 24;
        // Mặt nạ kênh theo THỨ TỰ BYTE trong bộ nhớ của fourcc: BGRX nghĩa là byte
        // 0 = B. Đọc thành số 32-bit little-endian thì B nằm ở bit thấp nhất.
        const bool bgr = vaFourcc == VA_FOURCC_BGRX || vaFourcc == VA_FOURCC_BGRA;
        fmt.red_mask = bgr ? 0x00FF0000u : 0x000000FFu;
        fmt.green_mask = 0x0000FF00u;
        fmt.blue_mask = bgr ? 0x000000FFu : 0x00FF0000u;
        fmt.alpha_mask = 0xFF000000u;
        if (!VaCheck(vaCreateImage(dpy_, &fmt, int(fi.width), int(fi.height), &rgbImage_),
                "vaCreateImage(RGB)")) {
            vaDestroySurfaces(dpy_, &rgbSurface_, 1);
            rgbSurface_ = VA_INVALID_SURFACE;
            return false;
        }
        haveRgbImage_ = true;
        rgbFourcc_ = vaFourcc;
    }

    uint8_t* dst = nullptr;
    if (!VaCheck(vaMapBuffer(dpy_, rgbImage_.buf, reinterpret_cast<void**>(&dst)), "vaMapBuffer"))
        return false;
    // Chép TỪNG DÒNG: stride của nguồn (PipeWire) và pitch của đích (VA image)
    // hiếm khi bằng nhau. Một memcpy nguyên khối sẽ lệch hình dần từ trên xuống.
    const uint32_t dstPitch = rgbImage_.pitches[0];
    const uint32_t rowBytes = fi.width * 4;
    const uint32_t copyBytes = rowBytes < dstPitch ? rowBytes : dstPitch;
    for (uint32_t y = 0; y < fi.height; ++y)
        std::memcpy(dst + rgbImage_.offsets[0] + size_t(y) * dstPitch,
            fi.data + size_t(y) * fi.stride, copyBytes);
    vaUnmapBuffer(dpy_, rgbImage_.buf);

    return VaCheck(vaPutImage(dpy_, rgbSurface_, rgbImage_.image_id, 0, 0, fi.width, fi.height, 0,
                       0, fi.width, fi.height),
        "vaPutImage");
}

bool VaEncoder::ConvertToNv12(VASurfaceID rgb) {
    // Vùng NGUỒN theo cỡ khung thật, vùng ĐÍCH theo cỡ nén. Hai số khác nhau thì
    // chính bước này co ảnh — không cần một bộ co riêng như bản Windows, vì Linux
    // chỉ có MỘT backend mã hoá và nó đã sẵn một pipeline VPP cho việc đổi màu.
    const uint32_t sw = srcW_ ? srcW_ : cfg_.width;
    const uint32_t sh = srcH_ ? srcH_ : cfg_.height;
    VARectangle srcRect{0, 0, uint16_t(sw), uint16_t(sh)};
    VARectangle dstRect{0, 0, uint16_t(cfg_.width), uint16_t(cfg_.height)};

    VAProcPipelineParameterBuffer pp{};
    pp.surface = rgb;
    pp.surface_region = &srcRect;
    pp.output_region = &dstRect;
    pp.surface_color_standard = VAProcColorStandardNone; // nguồn RGB, không có chuẩn
    // Phải KHỚP với colour_description ta ghi trong SPS — xem BuildParameterSets.
    pp.output_color_standard = VAProcColorStandardBT709;
    // Có co thật thì xin bộ lọc CHẤT LƯỢNG CAO: nội dung màn hình là chữ, và co
    // bằng bilinear thô làm chữ nhoè đúng thứ mà cả việc co pixel đang cố cứu.
    // Cùng cỡ thì DEFAULT — giá trị mọi driver chắc chắn nhận.
    pp.filter_flags = (sw != cfg_.width || sh != cfg_.height) ? VA_FILTER_SCALING_HQ
                                                             : VA_FILTER_SCALING_DEFAULT;

    VABufferID buf = VA_INVALID_ID;
    if (!VaCheck(vaCreateBuffer(dpy_, vppContext_, VAProcPipelineParameterBufferType, sizeof(pp),
                     1, &pp, &buf),
            "vaCreateBuffer(vpp)"))
        return false;

    bool ok = VaCheck(vaBeginPicture(dpy_, vppContext_, srcNv12_), "vaBeginPicture(vpp)");
    if (ok) ok = VaCheck(vaRenderPicture(dpy_, vppContext_, &buf, 1), "vaRenderPicture(vpp)");
    if (ok) ok = VaCheck(vaEndPicture(dpy_, vppContext_), "vaEndPicture(vpp)");
    vaDestroyBuffer(dpy_, buf);
    return ok;
}

// ---------------------------------------------------------------------------
// Nộp một frame cho bộ mã hoá
// ---------------------------------------------------------------------------
bool VaEncoder::EncodeNv12(bool idr, size_t& outSize) {
    outSize = 0;

    const uint32_t slot = uint32_t(frameCount_ & 1);
    const VASurfaceID recon = reconNv12_[slot];

    if (idr) {
        frameNum_ = 0;
        poc_ = 0;
        haveRef_ = false;
        ++idrPicId_;
    }

    // Danh sách buffer phải huỷ sau vaEndPicture. VA-API không tự thu hồi chúng;
    // rò ở đây là rò 60 buffer mỗi giây và driver sẽ hết bộ nhớ sau ít phút.
    std::vector<VABufferID> pending;
    pending.reserve(8);
    auto push = [&](VABufferID id) {
        if (id != VA_INVALID_ID) pending.push_back(id);
    };
    auto cleanup = [&] {
        for (VABufferID id : pending) vaDestroyBuffer(dpy_, id);
    };

    if (!VaCheck(vaBeginPicture(dpy_, encContext_, srcNv12_), "vaBeginPicture(enc)")) return false;

    // --- Sequence parameter: chỉ gửi ở IDR (và ở frame đầu tiên) ---
    if (idr) {
        VAEncSequenceParameterBufferH264 seq{};
        seq.seq_parameter_set_id = 0;
        seq.level_idc = LevelFor(mbW_, mbH_, cfg_.fps);
        // 0 = driver KHÔNG tự chèn I/IDR — xem "GOP vô hạn" ở đầu file.
        seq.intra_period = 0;
        seq.intra_idr_period = 0;
        seq.ip_period = 1; // không B-frame
        seq.bits_per_second = cfg_.bitrateBps;
        seq.max_num_ref_frames = kMaxRefFrames;
        seq.picture_width_in_mbs = uint16_t(mbW_);
        seq.picture_height_in_mbs = uint16_t(mbH_);
        seq.seq_fields.bits.chroma_format_idc = 1; // 4:2:0
        seq.seq_fields.bits.frame_mbs_only_flag = 1;
        seq.seq_fields.bits.direct_8x8_inference_flag = 1;
        seq.seq_fields.bits.log2_max_frame_num_minus4 = kLog2MaxFrameNumMinus4;
        seq.seq_fields.bits.pic_order_cnt_type = 0;
        seq.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 = kLog2MaxPocLsbMinus4;
        seq.frame_cropping_flag = (alignedW_ != cfg_.width || alignedH_ != cfg_.height) ? 1 : 0;
        seq.frame_crop_right_offset = (alignedW_ - cfg_.width) / 2;
        seq.frame_crop_bottom_offset = (alignedH_ - cfg_.height) / 2;
        seq.vui_parameters_present_flag = 1;
        seq.vui_fields.bits.timing_info_present_flag = 1;
        seq.vui_fields.bits.fixed_frame_rate_flag = 1;
        seq.num_units_in_tick = 1;
        seq.time_scale = 2 * (cfg_.fps ? cfg_.fps : 60);

        VABufferID id = VA_INVALID_ID;
        if (VaCheck(vaCreateBuffer(dpy_, encContext_, VAEncSequenceParameterBufferType,
                        sizeof(seq), 1, &seq, &id),
                "vaCreateBuffer(seq)")) {
            push(id);
            if (!VaCheck(vaRenderPicture(dpy_, encContext_, &id, 1), "vaRenderPicture(seq)")) {
                cleanup();
                vaEndPicture(dpy_, encContext_);
                return false;
            }
        }
    }

    // --- Rate control: gửi ở IDR và mỗi khi FEEDBACK đổi bitrate ---
    if (idr || pendingBitrate_) {
        VAEncMiscParameterRateControl* rc = nullptr;
        VABufferID id = MakeMiscBuffer(dpy_, encContext_, VAEncMiscParameterTypeRateControl, &rc);
        if (id != VA_INVALID_ID) {
            rc->bits_per_second = cfg_.bitrateBps;
            rc->target_percentage = 100; // CBR: bám sát mục tiêu
            rc->window_size = 500;       // cửa sổ điều tiết 500 ms
            rc->initial_qp = 0;          // 0 = để driver tự chọn
            rc->min_qp = 0;
            rc->max_qp = 0;
            // reset = 1 báo driver vứt trạng thái điều tiết cũ. Cần khi bitrate
            // thay đổi lớn, nếu không nó còn "nợ" bit của mức cũ và mất vài giây
            // mới hội tụ — quá chậm cho AIMD của BitrateController (docs/06 §6).
            rc->rc_flags.bits.reset = pendingBitrate_ ? 1 : 0;
            // Không cho driver bỏ frame: giao thức đếm frame để tính fps và độ
            // trễ, một frame bị nuốt im lặng làm cả hai số sai.
            rc->rc_flags.bits.disable_frame_skip = 1;
            vaUnmapBuffer(dpy_, id);
            push(id);
            vaRenderPicture(dpy_, encContext_, &id, 1);
        }

        VAEncMiscParameterHRD* hrd = nullptr;
        VABufferID hrdId = MakeMiscBuffer(dpy_, encContext_, VAEncMiscParameterTypeHRD, &hrd);
        if (hrdId != VA_INVALID_ID) {
            // Bộ đệm HRD nhỏ = encoder không được phép dồn bit vào một frame.
            // Đây là núm quan trọng nhất cho ĐỘ TRỄ: buffer lớn cho chất lượng
            // mượt hơn nhưng một IDR có thể phình lên hàng trăm KB và tắc nghẽn
            // đường truyền (đúng vấn đề "VBV không ăn trên QSV" của bản Windows,
            // docs/05 GĐ5). Nửa giây bitrate là thoả hiệp quen dùng cho stream
            // độ trễ thấp.
            hrd->buffer_size = cfg_.bitrateBps / 2;
            hrd->initial_buffer_fullness = hrd->buffer_size / 2;
            vaUnmapBuffer(dpy_, hrdId);
            push(hrdId);
            vaRenderPicture(dpy_, encContext_, &hrdId, 1);
        }

        VAEncMiscParameterFrameRate* fr = nullptr;
        VABufferID frId = MakeMiscBuffer(dpy_, encContext_, VAEncMiscParameterTypeFrameRate, &fr);
        if (frId != VA_INVALID_ID) {
            fr->framerate = cfg_.fps ? cfg_.fps : 60;
            vaUnmapBuffer(dpy_, frId);
            push(frId);
            vaRenderPicture(dpy_, encContext_, &frId, 1);
        }
        pendingBitrate_ = 0;
    }

    // --- Packed header SPS/PPS: đi kèm MỖI IDR (xem VaEncoder.h) ---
    if (idr && packedHeaders_) {
        struct {
            VAEncPackedHeaderType type;
            const std::vector<uint8_t>* data;
            uint32_t bits;
        } headers[2] = {
            {VAEncPackedHeaderSequence, &sps_, spsBits_},
            {VAEncPackedHeaderPicture, &pps_, ppsBits_},
        };
        for (const auto& h : headers) {
            VAEncPackedHeaderParameterBuffer ph{};
            ph.type = h.type;
            ph.bit_length = h.bits;
            ph.has_emulation_bytes = 1; // BitWriter đã chèn 0x03; driver đừng làm lại

            VABufferID pid = VA_INVALID_ID, did = VA_INVALID_ID;
            if (!VaCheck(vaCreateBuffer(dpy_, encContext_, VAEncPackedHeaderParameterBufferType,
                             sizeof(ph), 1, &ph, &pid),
                    "vaCreateBuffer(packed hdr param)"))
                continue;
            if (!VaCheck(vaCreateBuffer(dpy_, encContext_, VAEncPackedHeaderDataBufferType,
                             uint32_t(h.data->size()), 1,
                             const_cast<uint8_t*>(h.data->data()), &did),
                    "vaCreateBuffer(packed hdr data)")) {
                vaDestroyBuffer(dpy_, pid);
                continue;
            }
            push(pid);
            push(did);
            VABufferID both[2] = {pid, did};
            vaRenderPicture(dpy_, encContext_, both, 2);
        }
    }

    // --- Picture parameter ---
    VAEncPictureParameterBufferH264 pic{};
    pic.CurrPic.picture_id = recon; // surface TÁI DỰNG, không phải nguồn
    pic.CurrPic.frame_idx = frameNum_;
    pic.CurrPic.flags = 0;
    pic.CurrPic.TopFieldOrderCnt = poc_;
    pic.CurrPic.BottomFieldOrderCnt = poc_;
    for (auto& r : pic.ReferenceFrames) r = InvalidPic();
    if (haveRef_) {
        pic.ReferenceFrames[0].picture_id = refSurface_;
        pic.ReferenceFrames[0].frame_idx = refFrameNum_;
        pic.ReferenceFrames[0].flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        pic.ReferenceFrames[0].TopFieldOrderCnt = refPoc_;
        pic.ReferenceFrames[0].BottomFieldOrderCnt = refPoc_;
    }
    pic.coded_buf = codedBuf_;
    pic.pic_parameter_set_id = 0;
    pic.seq_parameter_set_id = 0;
    pic.last_picture = 0;
    pic.frame_num = uint16_t(frameNum_);
    pic.pic_init_qp = 26; // khớp pic_init_qp_minus26 = 0 trong PPS
    pic.num_ref_idx_l0_active_minus1 = 0;
    pic.num_ref_idx_l1_active_minus1 = 0;
    pic.chroma_qp_index_offset = 0;
    pic.second_chroma_qp_index_offset = 0;
    pic.pic_fields.bits.idr_pic_flag = idr ? 1 : 0;
    pic.pic_fields.bits.reference_pic_flag = 1;                     // mọi frame đều làm tham chiếu
    pic.pic_fields.bits.entropy_coding_mode_flag = 1;               // khớp PPS: CABAC
    pic.pic_fields.bits.deblocking_filter_control_present_flag = 1; // khớp PPS
    pic.pic_fields.bits.transform_8x8_mode_flag = 0;                // Main profile

    {
        VABufferID id = VA_INVALID_ID;
        if (!VaCheck(vaCreateBuffer(dpy_, encContext_, VAEncPictureParameterBufferType,
                         sizeof(pic), 1, &pic, &id),
                "vaCreateBuffer(pic)")) {
            cleanup();
            vaEndPicture(dpy_, encContext_);
            return false;
        }
        push(id);
        if (!VaCheck(vaRenderPicture(dpy_, encContext_, &id, 1), "vaRenderPicture(pic)")) {
            cleanup();
            vaEndPicture(dpy_, encContext_);
            return false;
        }
    }

    // --- Slice parameter: MỘT slice cho cả frame ---
    // Nhiều slice sẽ cho phép client giải mã một phần frame khi mất gói, nhưng
    // Reassembler hiện chỉ giao NAL trọn vẹn (docs/05 GĐ5, mục slicing) — chia
    // slice mà không sửa đường giải mã thì không được gì.
    VAEncSliceParameterBufferH264 slice{};
    slice.macroblock_address = 0;
    slice.num_macroblocks = mbW_ * mbH_;
    slice.macroblock_info = VA_INVALID_ID;
    slice.slice_type = idr ? 2 : 0; // 2 = I, 0 = P
    slice.pic_parameter_set_id = 0;
    slice.idr_pic_id = idrPicId_;
    slice.pic_order_cnt_lsb = uint16_t(poc_ & ((1 << (kLog2MaxPocLsbMinus4 + 4)) - 1));
    slice.num_ref_idx_active_override_flag = 0;
    slice.num_ref_idx_l0_active_minus1 = 0;
    slice.num_ref_idx_l1_active_minus1 = 0;
    for (auto& r : slice.RefPicList0) r = InvalidPic();
    for (auto& r : slice.RefPicList1) r = InvalidPic();
    if (!idr && haveRef_) slice.RefPicList0[0] = pic.ReferenceFrames[0];
    slice.cabac_init_idc = 0;
    // QP của frame này, tính TỪ kPicInitQp của PPS. Ở chế độ có CBR thì để 0 và
    // driver tự chọn QP theo bộ điều tiết của nó — chen vào sẽ đánh nhau với nó.
    // (iHD ở đường LP BỎ QUA pic_init_qp của picture param; slice_qp_delta mới là
    // núm thật — đo được: QP 20→42 cho ra 64→13 Mbps qua delta, còn đổi pic_init_qp
    // không làm kích thước nhúc nhích.)
    const int frameQp = idr ? IdrQp() : std::clamp(qp_, kQpMin, kQpMax);
    slice.slice_qp_delta = cqpMode_ ? int8_t(frameQp - kPicInitQp) : 0;
    slice.disable_deblocking_filter_idc = 0;

    {
        VABufferID id = VA_INVALID_ID;
        if (!VaCheck(vaCreateBuffer(dpy_, encContext_, VAEncSliceParameterBufferType,
                         sizeof(slice), 1, &slice, &id),
                "vaCreateBuffer(slice)")) {
            cleanup();
            vaEndPicture(dpy_, encContext_);
            return false;
        }
        push(id);
        if (!VaCheck(vaRenderPicture(dpy_, encContext_, &id, 1), "vaRenderPicture(slice)")) {
            cleanup();
            vaEndPicture(dpy_, encContext_);
            return false;
        }
    }

    const bool ended = VaCheck(vaEndPicture(dpy_, encContext_), "vaEndPicture(enc)");
    cleanup();
    if (!ended) return false;

    // Chờ GPU xong. Đồng bộ trên surface NGUỒN vì đó là surface ta đưa cho
    // vaBeginPicture — VA-API gắn công việc vào nó.
    if (!VaCheck(vaSyncSurface(dpy_, srcNv12_), "vaSyncSurface")) return false;

    // --- Đọc bitstream ---
    VACodedBufferSegment* seg = nullptr;
    if (!VaCheck(vaMapBuffer(dpy_, codedBuf_, reinterpret_cast<void**>(&seg)), "vaMapBuffer(coded)"))
        return false;
    out_.clear();
    bool overflow = false;
    for (VACodedBufferSegment* s = seg; s; s = static_cast<VACodedBufferSegment*>(s->next)) {
        if (s->status & VA_CODED_BUF_STATUS_SLICE_OVERFLOW_MASK) overflow = true;
        const auto* p = static_cast<const uint8_t*>(s->buf);
        out_.insert(out_.end(), p, p + s->size);
    }
    vaUnmapBuffer(dpy_, codedBuf_);

    if (overflow) {
        LOGW("[VaEnc] Coded buffer overflowed — frame dropped.");
        return false;
    }
    if (out_.empty()) return false;

    // --- Vòng điều tiết QP ---
    if (cqpMode_) {
        const double fps = double(cfg_.fps ? cfg_.fps : 60);
        const uint64_t nowUs = NowUs();
        // Hạn mức theo THỜI GIAN THẬT đã trôi, không theo cfg_.fps. Capture bám theo
        // damage của compositor nên fps thật dao động dữ dội — trong log đo được từ
        // 2 tới 137 fps trên cùng một phiên. Cấp hạn mức theo fps danh nghĩa nghĩa
        // là ở 137 fps ta phát gấp hơn hai lần mục tiêu, đúng lúc màn hình động
        // nhất và đường truyền ít chịu nổi nhất.
        const double frameUs = 1e6 / fps;
        const double elapsedUs = lastEncodeUs_
                                     ? std::clamp(double(nowUs - lastEncodeUs_), 0.0, frameUs * kMaxBudgetFrames)
                                     : frameUs;
        lastEncodeUs_ = nowUs;
        const double budget = double(cfg_.bitrateBps) / 8.0 * elapsedUs / 1e6;

        // IDR KHÔNG nuôi trung bình trượt. Nó lớn gấp cả chục lần P-frame vì bản
        // chất I-frame, không phải vì QP sai, nên để nó vào là bơm một cú nhảy vào
        // đúng tín hiệu ta vừa mất công lọc. Bù lại, IDR chịu +kIdrQpDelta và co
        // giãn theo qp_ mà vòng này giữ — nên nó vẫn nhỏ đi khi mục tiêu nhỏ đi.
        if (idr) {
            lastIdrQp_ = frameQp;
            lastIdrBytes_ = out_.size();
        }
        if (!idr && budget > 1.0) {
            const double logRatio = std::log(double(out_.size() ? out_.size() : 1) / budget);
            const double a = 1.0 / kEmaFrames;
            logRatioEma_ = haveRatio_ ? logRatioEma_ * (1.0 - a) + logRatio * a : logRatio;
            haveRatio_ = true;
            const int step = std::clamp(int(std::lround(logRatioEma_ / std::log(1.125))),
                -kQpStepMax, kQpStepMax);
            qp_ = std::clamp(qp_ + step, kQpMin, kQpMax);
        }
    }

    // --- Cập nhật trạng thái chuỗi cho frame sau ---
    refSurface_ = recon;
    refFrameNum_ = frameNum_;
    refPoc_ = poc_;
    haveRef_ = true;
    frameNum_ = (frameNum_ + 1) & ((1u << (kLog2MaxFrameNumMinus4 + 4)) - 1);
    poc_ += 2; // không B-frame nên POC tăng đều 2 mỗi frame
    ++frameCount_;

    outSize = out_.size();
    return true;
}

// ---------------------------------------------------------------------------
// Encode — điểm vào công khai
// ---------------------------------------------------------------------------
bool VaEncoder::Encode(const LinuxFrameInfo& fi, uint64_t timestampUs, bool forceKeyframe) {
    if (!IsOpen()) return false;
    // Kích thước nguồn đổi giữa chừng: AgentLoop chịu trách nhiệm dựng lại encoder
    // (nó theo dõi qua cờ sizeChanged). Ở đây chỉ từ chối, không tự dựng lại — tự
    // dựng lại sẽ nuốt mất tín hiệu mà AgentLoop cần để gửi RECONFIG cho client.
    //
    // Log MỘT LẦN, vì đây là đường hỏng IM LẶNG duy nhất còn lại của Encode và nó
    // hỏng VĨNH VIỄN chứ không phải một frame: AgentLoop dựng encoder theo
    // (fi.width & ~1) nhưng so ở đây là fi.width THÔ, nên một nguồn có bề rộng LẺ
    // cho ra vòng lặp mọi frame đều false mà không một dòng log nào. Nhìn từ ngoài
    // y hệt ca dma-buf: capture chạy, encode hỏng 100%, không manh mối.
    // Khung TO HƠN cỡ nén là hợp lệ: bước VPP ngay dưới vừa đổi màu vừa co
    // (deskhub::StreamSize / QualityLadder quyết định co bao nhiêu). Chỉ khung NHỎ
    // HƠN mới là lỗi — phóng to ở host chỉ đốt bitrate cho pixel nội suy.
    if (fi.width < cfg_.width || fi.height < cfg_.height) {
        static thread_local bool warned = false;
        if (!warned) {
            warned = true;
            LOGE(
                "[VaEnc] Frame is %ux%u but the encoder was built for %ux%u — dropping every "
                "frame until it is rebuilt.",
                fi.width, fi.height, cfg_.width, cfg_.height);
        }
        return false;
    }
    // VPP đọc hai số này để biết vùng nguồn. Ghi ở đây, ngay trước khi surface RGB
    // được dựng/nhập theo đúng cỡ đó.
    srcW_ = fi.width;
    srcH_ = fi.height;

    // Frame đầu tiên của một chuỗi BẮT BUỘC là IDR: không có nó thì client nhận
    // một chuỗi P-frame không có gốc và giải ra rác.
    const bool idr = forceKeyframe || !haveRef_;

    lastDmaBuf_ = fi.memory == FrameMemory::DmaBuf;

    VASurfaceID rgb = VA_INVALID_SURFACE;
    bool imported = false;
    if (fi.memory == FrameMemory::DmaBuf) {
        rgb = ImportDmaBuf(fi);
        imported = rgb != VA_INVALID_SURFACE;
        if (!imported) {
            // Log MỘT LẦN mỗi encoder: driver không import được modifier này thì
            // frame nào cũng thế, và log mỗi frame sẽ nhấn chìm mọi thứ khác.
            static thread_local bool warned = false;
            if (!warned) {
                warned = true;
                LOGE(
                    "[VaEnc] Cannot import the compositor's dma-buf (modifier 0x%llx). "
                    "The GPU driver and the compositor disagree on buffer layout.",
                    (unsigned long long)fi.modifier);
            }
            return false;
        }
    } else {
        if (!UploadMapped(fi)) return false;
        rgb = rgbSurface_;
    }

    bool ok = ConvertToNv12(rgb);
    if (ok) haveSource_ = true; // srcNv12_ giờ có nội dung thật -> EncodeLast dùng được
    size_t size = 0;
    if (ok) ok = EncodeNv12(idr, size);

    // Surface import phải huỷ NGAY: fd của nó chỉ hợp lệ trong callback capture.
    if (imported) vaDestroySurfaces(dpy_, &rgb, 1);

    if (ok && cfg_.onPacket) cfg_.onPacket(out_.data(), size, timestampUs, idr);
    return ok;
}

// Không đụng tới capture lẫn VPP: srcNv12_ vẫn giữ nguyên nội dung của lần
// ConvertToNv12 gần nhất — xem ⚠ ở VaEncoder.h.
bool VaEncoder::EncodeLast(uint64_t timestampUs, bool forceKeyframe) {
    if (!IsOpen() || !haveSource_) return false;
    const bool idr = forceKeyframe || !haveRef_;
    size_t size = 0;
    if (!EncodeNv12(idr, size)) return false;
    if (cfg_.onPacket) cfg_.onPacket(out_.data(), size, timestampUs, idr);
    return true;
}
