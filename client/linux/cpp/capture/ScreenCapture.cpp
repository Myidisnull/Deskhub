// =============================================================================
// ScreenCapture.cpp — cài đặt bằng pw_stream (PipeWire).
//
// BỐ CỤC
//   Bảng dịch định dạng SPA ↔ DRM fourcc.
//   BuildFormat()      — dựng pod EnumFormat (hai bản: có/không modifier).
//   OnParamChanged()   — thoả thuận định dạng + KHAI BÁO KIỂU BUFFER mong muốn.
//   OnProcess()        — đường nóng: lấy buffer mới nhất → dựng LinuxFrameInfo → callback.
//   OnStateChanged()   — theo dõi node chết để AgentLoop gỡ nguồn.
//
// ⚠ BỐN CÁI BẪY CỦA PIPEWIRE, GHI LẠI ĐỂ KHÔNG DẪM LẠI
//
//   1. pw_context_connect_fd() ĐÓNG fd NÓ NHẬN, kể cả khi thất bại. Portal chỉ cho
//      ta MỘT fd cho tất cả màn hình, nên mỗi ScreenCapture phải truyền một bản
//      dup() riêng. Truyền thẳng fd của portal thì màn hình thứ hai nhận một fd đã
//      đóng, và lỗi hiện ra ở chỗ hoàn toàn khác.
//
//   2. VÉT HẾT HÀNG ĐỢI, CHỈ LẤY FRAME MỚI NHẤT. pw_stream_dequeue_buffer trả về
//      frame CŨ NHẤT chưa xử lý. Nếu encoder chậm hơn compositor một nhịp, hàng đợi
//      dồn lại và ta sẽ luôn nén frame cũ — độ trễ tăng dần và không bao giờ tự
//      hồi. Vòng vét ở OnProcess trả sớm mọi buffer trừ cái cuối.
//
//   3. THOẢ THUẬN MODIFIER LÀ HAI NHỊP. Khi ta chào modifier với cờ DONT_FIXATE,
//      PipeWire trả về một danh sách modifier chứ không phải một giá trị — ta phải
//      CHỌN một cái rồi update_params lần nữa, và param_changed sẽ được gọi lại.
//      Bỏ nhịp thứ hai thì stream đứng im ở trạng thái đang thoả thuận, không frame
//      nào tới, và không có thông báo lỗi nào.
//
//   4. spa_pod_builder GHI VÀO BỘ ĐỆM CỦA NGƯỜI GỌI. Pod trả về chỉ là con trỏ vào
//      bộ đệm đó, nên bộ đệm phải còn sống tới khi pw_stream_connect/update_params
//      chạy xong. Để nó là biến cục bộ của cùng một hàm là đúng; đừng "gọn hoá"
//      bằng cách tách BuildFormat ra rồi trả pod về từ một bộ đệm đã hết phạm vi.
//
// LIÊN QUAN: capture/ScreenCapture.h (lý do thiết kế), capture/CaptureTypes.h
// =============================================================================
#include "capture/ScreenCapture.h"

#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>
#include <spa/utils/result.h>

#include <drm_fourcc.h>

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "deskhubp/Log.h"
#include "deskhubp/Clock.h"

namespace {

// pw_init phải chạy đúng một lần cho cả tiến trình, dù có bao nhiêu stream.
std::once_flag g_pwInit;

// SPA nói theo THỨ TỰ BYTE TRONG BỘ NHỚ (BGRx = byte B, G, R, x), DRM nói theo giá
// trị 32-bit little-endian (XRGB8888 = 0xXXRRGGBB = byte B, G, R, X). Hai cách gọi
// tên ngược nhau nên bảng này trông "lộn" — nó đúng, đừng sửa theo trực giác.
uint32_t SpaToDrmFormat(uint32_t spaFormat) {
    switch (spaFormat) {
        case SPA_VIDEO_FORMAT_BGRx: return DRM_FORMAT_XRGB8888;
        case SPA_VIDEO_FORMAT_BGRA: return DRM_FORMAT_ARGB8888;
        case SPA_VIDEO_FORMAT_RGBx: return DRM_FORMAT_XBGR8888;
        case SPA_VIDEO_FORMAT_RGBA: return DRM_FORMAT_ABGR8888;
        default: return 0;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Trạng thái nội bộ
// ---------------------------------------------------------------------------
struct ScreenCapture::Impl {
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_stream* stream = nullptr;
    spa_hook streamListener{};

    uint32_t nodeId = 0;
    uint32_t fps = 60;
    ScreenCapture::FrameHandler onFrame;

    // Định dạng đã thoả thuận. Chỉ thread PipeWire chạm (param_changed và process
    // đều chạy trên nó).
    spa_video_info_raw format{};
    uint32_t drmFormat = 0;
    bool haveFormat = false;
    bool wantDmaBuf = false; // modifier đã được chốt -> buffer sẽ là dma-buf

    std::atomic<bool> closed{false};
    std::atomic<bool> dmaBufActive{false};

    // Danh sách modifier ta chấp nhận, theo thứ tự ưu tiên.
    //
    // ⚠ LINEAR ĐỨNG TRƯỚC INVALID, VÀ THỨ TỰ NÀY LÀ CÓ CHỦ Ý.
    // Trước đây INVALID đứng đầu với lý do "gần như mọi driver hỗ trợ". Lý do đó
    // lẫn hai phía: INVALID được chấp nhận rộng rãi ở bước THOẢ THUẬN, nhưng nó
    // không phải một layout, nên phía IMPORT không thể nói cho driver buffer trông
    // như thế nào. Kết quả trên Intel/iHD là import hỏng 100% (xem ⚠ ở
    // VaEncoder.cpp::ImportDmaBuf). LINEAR thì không nhập nhằng: cả compositor và
    // driver hiểu đúng một layout, nên import luôn chạy.
    // INVALID vẫn giữ làm lối lùi cho compositor không chào LINEAR — ImportDmaBuf
    // xử lý nó bằng đường DRM_PRIME cũ — nhưng đó là đường ĐOÁN layout, nên chỉ
    // dùng khi không còn lựa chọn nào.
    static constexpr uint64_t kModifiers[] = {DRM_FORMAT_MOD_LINEAR, DRM_FORMAT_MOD_INVALID};
};

namespace {

// Dựng một pod EnumFormat. `modifiers` = nullptr -> bản KHÔNG modifier (lối lùi
// MemFd/MemPtr). Xem bẫy số 4 ở đầu file về vòng đời của bộ đệm.
const spa_pod* BuildFormat(spa_pod_builder* b, const uint64_t* modifiers, size_t modifierCount,
    uint32_t maxFps) {
    spa_pod_frame f0{}, f1{};

    const spa_rectangle defSize{1920, 1080};
    const spa_rectangle minSize{16, 16};
    const spa_rectangle maxSize{8192, 8192};
    const spa_fraction defFps{maxFps, 1};
    const spa_fraction minFps{0, 1};
    const spa_fraction maxFpsFrac{maxFps, 1};

    spa_pod_builder_push_object(b, &f0, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
    spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);

    // Bốn định dạng RGB 32-bit. CỐ Ý KHÔNG chào NV12/I420: compositor Wayland đưa
    // ra bộ đệm màn hình ở dạng RGB, và bắt nó tự chuyển sang NV12 chỉ đẩy phép đổi
    // màu sang một chỗ ta không kiểm soát được. VaEncoder chuyển RGB→NV12 bằng VPP
    // trên GPU (encode/VaEncoder.h §"đường màu").
    spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_format, 0);
    spa_pod_builder_push_choice(b, &f1, SPA_CHOICE_Enum, 0);
    spa_pod_builder_id(b, SPA_VIDEO_FORMAT_BGRx); // giá trị mặc định
    spa_pod_builder_id(b, SPA_VIDEO_FORMAT_BGRx);
    spa_pod_builder_id(b, SPA_VIDEO_FORMAT_BGRA);
    spa_pod_builder_id(b, SPA_VIDEO_FORMAT_RGBx);
    spa_pod_builder_id(b, SPA_VIDEO_FORMAT_RGBA);
    spa_pod_builder_pop(b, &f1);

    if (modifiers && modifierCount) {
        // MANDATORY + DONT_FIXATE = "tôi muốn dma-buf, và đây là các layout tôi
        // nhận; hãy trả về phần giao chứ đừng chốt hộ tôi". Nhịp chốt nằm ở
        // OnParamChanged — bẫy số 3 ở đầu file.
        spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier,
            SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(b, &f1, SPA_CHOICE_Enum, 0);
        spa_pod_builder_long(b, int64_t(modifiers[0])); // giá trị mặc định
        for (size_t i = 0; i < modifierCount; ++i) spa_pod_builder_long(b, int64_t(modifiers[i]));
        spa_pod_builder_pop(b, &f1);
    }

    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size,
        SPA_POD_CHOICE_RANGE_Rectangle(&defSize, &minSize, &maxSize), 0);
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate,
        SPA_POD_CHOICE_RANGE_Fraction(&defFps, &minFps, &maxFpsFrac), 0);

    return static_cast<const spa_pod*>(spa_pod_builder_pop(b, &f0));
}

// Dựng pod Format ĐÃ CHỐT một modifier duy nhất — nhịp thứ hai của bẫy số 3.
const spa_pod* BuildFixatedFormat(spa_pod_builder* b, const spa_video_info_raw& info,
    uint64_t modifier, uint32_t maxFps) {
    spa_pod_frame f0{};
    const spa_fraction defFps{maxFps, 1};
    const spa_fraction minFps{0, 1};
    const spa_fraction maxFpsFrac{maxFps, 1};

    spa_pod_builder_push_object(b, &f0, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
    spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(info.format), 0);
    spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY);
    spa_pod_builder_long(b, int64_t(modifier));
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_size, SPA_POD_Rectangle(&info.size), 0);
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate,
        SPA_POD_CHOICE_RANGE_Fraction(&defFps, &minFps, &maxFpsFrac), 0);
    return static_cast<const spa_pod*>(spa_pod_builder_pop(b, &f0));
}

void OnStateChanged(void* data, pw_stream_state old, pw_stream_state state, const char* error) {
    auto* im = static_cast<ScreenCapture::Impl*>(data);
    LOGI("[Capture][node %u] state %s -> %s%s%s", im->nodeId, pw_stream_state_as_string(old),
        pw_stream_state_as_string(state), error ? ": " : "", error ? error : "");
    // UNCONNECTED = node biến mất (người dùng bấm "Stop sharing" trên chỉ báo của
    // compositor). ERROR = hỏng không hồi phục. Cả hai đều là "nguồn này đã chết";
    // AgentLoop đọc Closed() rồi gỡ nó khỏi phiên.
    if (state == PW_STREAM_STATE_UNCONNECTED || state == PW_STREAM_STATE_ERROR)
        im->closed.store(true, std::memory_order_release);
}

void OnParamChanged(void* data, uint32_t id, const spa_pod* param) {
    auto* im = static_cast<ScreenCapture::Impl*>(data);
    if (!param || id != SPA_PARAM_Format) return;

    uint32_t mediaType = 0, mediaSubtype = 0;
    if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0) return;
    if (mediaType != SPA_MEDIA_TYPE_video || mediaSubtype != SPA_MEDIA_SUBTYPE_raw) return;

    spa_video_info_raw info{};
    if (spa_format_video_raw_parse(param, &info) < 0) {
        LOGE("[Capture][node %u] Could not parse the negotiated format.", im->nodeId);
        return;
    }

    // --- Nhịp 1 của thoả thuận modifier: PipeWire trả về DANH SÁCH, ta phải chốt ---
    const spa_pod_prop* modProp = spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier);
    uint8_t podBuf[1024];
    if (modProp && (modProp->flags & SPA_POD_PROP_FLAG_DONT_FIXATE)) {
        // PipeWire trả về PHẦN GIAO giữa danh sách ta chào và danh sách compositor
        // làm được, dưới dạng một pod Choice (KHÔNG phải Array — spa_pod_get_array
        // trả NULL ở đây, đừng dùng nó). spa_pod_get_values bóc được cả hai kiểu và
        // trả về pod con mô tả kiểu phần tử; các giá trị nằm ngay sau nó.
        uint32_t nVals = 0, choiceType = 0;
        spa_pod* child = spa_pod_get_values(&modProp->value, &nVals, &choiceType);
        const int64_t* mods = (child && SPA_POD_TYPE(child) == SPA_TYPE_Long)
                                  ? static_cast<const int64_t*>(SPA_POD_BODY(child))
                                  : nullptr;

        // Chọn theo THỨ TỰ ƯU TIÊN CỦA TA trong phần giao, chứ không nhắm mắt lấy
        // phần tử đầu: phần tử đầu là mặc định của compositor, mà thứ ta import
        // được vào VA-API mới là thứ quyết định. Không khớp cái nào (phần giao rỗng
        // hoặc pod dị dạng) thì lùi về INVALID — driver tự chọn layout ngầm định.
        uint64_t chosen = DRM_FORMAT_MOD_INVALID;
        bool found = false;
        for (uint64_t want : ScreenCapture::Impl::kModifiers) {
            for (uint32_t i = 0; mods && i < nVals && !found; ++i)
                if (uint64_t(mods[i]) == want) {
                    chosen = want;
                    found = true;
                }
            if (found) break;
        }
        if (!found && mods && nVals) chosen = uint64_t(mods[0]);

        spa_pod_builder b = SPA_POD_BUILDER_INIT(podBuf, sizeof(podBuf));
        const spa_pod* fixed[1] = {BuildFixatedFormat(&b, info, chosen, im->fps)};
        LOGI("[Capture][node %u] Fixating dma-buf modifier 0x%llx.", im->nodeId,
            (unsigned long long)chosen);
        pw_stream_update_params(im->stream, fixed, 1);
        return; // param_changed sẽ được gọi lại với định dạng đã chốt
    }

    // --- Định dạng đã chốt: ghi nhận và khai báo kiểu buffer mong muốn ---
    im->format = info;
    im->drmFormat = SpaToDrmFormat(info.format);
    im->wantDmaBuf = modProp != nullptr;
    im->haveFormat = im->drmFormat != 0;
    if (!im->haveFormat) {
        LOGE("[Capture][node %u] Negotiated an unsupported pixel format (%u).", im->nodeId,
            info.format);
        im->closed.store(true, std::memory_order_release);
        return;
    }

    LOGI("[Capture][node %u] Format: %ux%u %s, %s.", im->nodeId, info.size.width, info.size.height,
        spa_debug_type_find_short_name(spa_type_video_format, info.format),
        im->wantDmaBuf ? "dma-buf (zero-copy)" : "mapped memory (CPU copy)");

    // dataType quyết định compositor cấp buffer kiểu gì. Khai báo đúng một kiểu cho
    // mỗi đường: xin cả hai cùng lúc thì OnProcess phải chịu được buffer trộn lẫn
    // giữa các frame, mà không đường nào cần điều đó.
    const int dataType = im->wantDmaBuf ? (1 << SPA_DATA_DmaBuf)
                                        : ((1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr));

    spa_pod_builder b = SPA_POD_BUILDER_INIT(podBuf, sizeof(podBuf));
    const spa_pod* params[1];
    params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        // 4 buffer: đủ để compositor không phải chờ ta trả buffer, đủ ít để một
        // frame không nằm chờ lâu trong hàng đợi (xem bẫy số 2).
        SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 8),
        SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(dataType)));
    pw_stream_update_params(im->stream, params, 1);
}

void OnProcess(void* data) {
    auto* im = static_cast<ScreenCapture::Impl*>(data);

    // Bẫy số 2: vét hàng đợi, chỉ giữ frame MỚI NHẤT.
    pw_buffer* b = pw_stream_dequeue_buffer(im->stream);
    if (!b) return;
    while (pw_buffer* next = pw_stream_dequeue_buffer(im->stream)) {
        pw_stream_queue_buffer(im->stream, b);
        b = next;
    }

    spa_buffer* sb = b->buffer;
    if (!im->haveFormat || !sb->n_datas) {
        pw_stream_queue_buffer(im->stream, b);
        return;
    }

    LinuxFrameInfo fi;
    fi.drmFormat = im->drmFormat;
    // Làm tròn XUỐNG số chẵn: H.264 lấy mẫu chroma theo khối 2×2 và VA-API từ chối
    // kích thước lẻ (cùng lý do với bản macOS, xem CaptureTypes.h).
    fi.width = im->format.size.width & ~1u;
    fi.height = im->format.size.height & ~1u;
    fi.timestampUs = NowUs();

    bool ok = false;
    if (sb->datas[0].type == SPA_DATA_DmaBuf) {
        fi.memory = FrameMemory::DmaBuf;
        fi.modifier = im->format.modifier;
        fi.planeCount = sb->n_datas > kMaxDmaPlanes ? kMaxDmaPlanes : sb->n_datas;
        for (uint32_t i = 0; i < fi.planeCount; ++i) {
            fi.planes[i].fd = int(sb->datas[i].fd);
            fi.planes[i].offset = sb->datas[i].chunk->offset;
            fi.planes[i].stride = uint32_t(sb->datas[i].chunk->stride);
        }
        ok = fi.planeCount > 0 && fi.planes[0].fd >= 0;
        im->dmaBufActive.store(true, std::memory_order_relaxed);
    } else if (sb->datas[0].data) {
        fi.memory = FrameMemory::Mapped;
        fi.data = static_cast<const uint8_t*>(sb->datas[0].data) + sb->datas[0].chunk->offset;
        fi.stride = uint32_t(sb->datas[0].chunk->stride);
        // chunk->size == 0 là frame RỖNG, không phải lỗi: compositor dùng nó để báo
        // "không có gì đổi". Bỏ qua chứ đừng nén một khung đen.
        ok = sb->datas[0].chunk->size > 0 && fi.stride > 0;
        im->dmaBufActive.store(false, std::memory_order_relaxed);
    }

    // CORRUPTED = compositor báo nội dung frame này không tin được (thường do đổi
    // cấu hình màn hình giữa chừng). Nén nó ra sẽ cho một khung rác đi thẳng tới
    // người xem.
    if (sb->datas[0].chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) ok = false;

    if (ok && fi.width && fi.height && im->onFrame) im->onFrame(fi);

    pw_stream_queue_buffer(im->stream, b);
}

const pw_stream_events kStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .destroy = nullptr,
    .state_changed = OnStateChanged,
    .control_info = nullptr,
    .io_changed = nullptr,
    .param_changed = OnParamChanged,
    .add_buffer = nullptr,
    .remove_buffer = nullptr,
    .process = OnProcess,
    .drained = nullptr,
    .command = nullptr,
    .trigger_done = nullptr,
};

} // namespace

ScreenCapture::ScreenCapture() : impl_(std::make_unique<Impl>()) {}

ScreenCapture::~ScreenCapture() {
    Stop();
}

bool ScreenCapture::Closed() const {
    return impl_ && impl_->closed.load(std::memory_order_acquire);
}

bool ScreenCapture::usingDmaBuf() const {
    return impl_ && impl_->dmaBufActive.load(std::memory_order_relaxed);
}

bool ScreenCapture::Start(int portalFd, uint32_t nodeId, uint32_t fps, FrameHandler onFrame) {
    std::call_once(g_pwInit, [] { pw_init(nullptr, nullptr); });

    Impl* im = impl_.get();
    im->nodeId = nodeId;
    im->fps = fps ? fps : 60;
    im->onFrame = std::move(onFrame);
    im->closed.store(false);

    im->loop = pw_thread_loop_new("deskhub-capture", nullptr);
    if (!im->loop) {
        LOGE("[Capture][node %u] pw_thread_loop_new failed.", nodeId);
        return false;
    }

    pw_thread_loop_lock(im->loop);

    im->context = pw_context_new(pw_thread_loop_get_loop(im->loop), nullptr, 0);
    if (!im->context) {
        LOGE("[Capture][node %u] pw_context_new failed.", nodeId);
        pw_thread_loop_unlock(im->loop);
        Stop();
        return false;
    }

    // Bẫy số 1: bản dup riêng, vì pw_context_connect_fd() đóng fd nó nhận.
    const int fd = fcntl(portalFd, F_DUPFD_CLOEXEC, 3);
    if (fd < 0) {
        LOGE("[Capture][node %u] Could not dup the portal fd.", nodeId);
        pw_thread_loop_unlock(im->loop);
        Stop();
        return false;
    }
    im->core = pw_context_connect_fd(im->context, fd, nullptr, 0);
    if (!im->core) {
        LOGE("[Capture][node %u] pw_context_connect_fd failed.", nodeId);
        pw_thread_loop_unlock(im->loop);
        Stop();
        return false;
    }

    pw_properties* props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
        "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr);

    im->stream = pw_stream_new(im->core, "deskhub-capture", props); // props bị stream nuốt
    if (!im->stream) {
        LOGE("[Capture][node %u] pw_stream_new failed.", nodeId);
        pw_thread_loop_unlock(im->loop);
        Stop();
        return false;
    }
    pw_stream_add_listener(im->stream, &im->streamListener, &kStreamEvents, im);

    // Hai bản EnumFormat: có modifier (dma-buf) trước, không modifier (lối lùi) sau.
    // PipeWire thử theo thứ tự, nên compositor nào làm được dma-buf sẽ chọn bản đầu.
    uint8_t podBuf[2048];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(podBuf, sizeof(podBuf));
    const spa_pod* params[2];
    params[0] = BuildFormat(&b, Impl::kModifiers,
        sizeof(Impl::kModifiers) / sizeof(Impl::kModifiers[0]), im->fps);
    params[1] = BuildFormat(&b, nullptr, 0, im->fps);

    // MAP_BUFFERS: PipeWire tự mmap buffer MemFd cho ta (nhánh Mapped). Với dma-buf
    // nó không map gì — đúng như ta muốn, vì ta import fd thẳng vào VA-API.
    //
    // ⚠ CHỈ ĐỊNH NODE BẰNG target_id, KHÔNG PHẢI PW_KEY_TARGET_OBJECT.
    // Property `target.object` mới là đường được khuyến nghị và target_id bị đánh
    // dấu lỗi thời TRONG TÀI LIỆU — nhưng property đó chỉ được pw_stream THỰC SỰ
    // tôn trọng từ 0.3.64, còn Ubuntu 22.04 vẫn ở 0.3.48. Ở đó hằng số
    // PW_KEY_TARGET_OBJECT có tồn tại (nên code vẫn biên dịch được, không có lỗi
    // nào lộ ra) nhưng stream sẽ nối vào node mặc định thay vì màn hình người dùng
    // chọn — đúng kiểu hỏng im lặng khó lần nhất. target_id thì được cả 0.3.48 lẫn
    // 1.x tôn trọng, nên đây là đường DUY NHẤT chạy trên mọi bản ta hỗ trợ.
    const int rc = pw_stream_connect(im->stream, PW_DIRECTION_INPUT, nodeId,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        params, 2);
    pw_thread_loop_unlock(im->loop);

    if (rc < 0) {
        LOGE("[Capture][node %u] pw_stream_connect failed: %s", nodeId, spa_strerror(rc));
        Stop();
        return false;
    }

    if (pw_thread_loop_start(im->loop) < 0) {
        LOGE("[Capture][node %u] pw_thread_loop_start failed.", nodeId);
        Stop();
        return false;
    }
    return true;
}

// Thứ tự tháo dỡ là BẮT BUỘC: dừng thread loop TRƯỚC rồi mới huỷ stream/core/
// context. Huỷ trong lúc thread loop còn chạy là dùng đối tượng đã chết ngay trong
// callback đang thực thi.
void ScreenCapture::Stop() {
    Impl* im = impl_.get();
    if (!im) return;

    if (im->loop) pw_thread_loop_stop(im->loop);
    if (im->stream) {
        pw_stream_destroy(im->stream);
        im->stream = nullptr;
    }
    if (im->core) {
        pw_core_disconnect(im->core);
        im->core = nullptr;
    }
    if (im->context) {
        pw_context_destroy(im->context);
        im->context = nullptr;
    }
    if (im->loop) {
        pw_thread_loop_destroy(im->loop);
        im->loop = nullptr;
    }
    im->closed.store(true, std::memory_order_release);
}
