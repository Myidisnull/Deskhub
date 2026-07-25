// =============================================================================
// ScreenCapture.mm — cài đặt bằng SCStream.
//
// BỐ CỤC
//   DeskhubStreamOutput — đối tượng Obj-C nhận frame (SCStreamOutput) và nhận tin
//                         stream chết (SCStreamDelegate). Giữ con trỏ về Impl.
//   Impl                — trạng thái C++: stream, cấu hình hiện tại, bộ theo dõi cỡ.
//   Start/Stop/Closed   — vòng đời, khớp API của WindowCapture bên Windows.
//
// VÌ SAO CÓ BỘ THEO DÕI CỠ (dispatch timer 500ms)
//   SCStreamConfiguration.width/height là cỡ BUFFER, cố định lúc tạo stream. Người
//   dùng kéo cửa sổ to ra thì SCStream vẫn giao buffer cũ với nội dung bị co lại —
//   không có sự kiện nào báo. Không theo dõi thì người xem thấy hình mờ dần và méo
//   tỉ lệ mà không hiểu vì sao. Bộ đếm này so cỡ nguồn với cỡ buffer, lệch thì gọi
//   updateConfiguration; frame kế tiếp về đúng cỡ mới và AgentLoop nhận ra qua đường
//   "sizeChanged" y hệt bản Windows.
//
//   Nó cũng là nơi phát hiện CỬA SỔ ĐÃ ĐÓNG: CGWindowListCopyWindowInfo trả về mảng
//   rỗng cho một windowID không còn tồn tại. SCStream không báo gì khi cửa sổ đóng —
//   nó chỉ ngừng phát frame, và một nguồn im lặng vĩnh viễn nhìn y như mạng hỏng.
//
// VÌ SAO 500ms CHỨ KHÔNG NHANH HƠN
//   Đổi cỡ là thao tác của con người (kéo chuột), không phải sự kiện realtime. Mỗi
//   lần đổi cấu hình là một lần encoder phía trên phải dựng lại và phát IDR — đắt.
//   500ms đủ để người dùng không thấy trễ mà không biến một cú kéo cửa sổ thành hàng
//   chục lần dựng lại encoder.
//
// LIÊN QUAN: agent/ScreenCapture.h (bốn quyết định thiết kế)
// =============================================================================
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include "agent/ScreenCapture.h"

#include <atomic>
#include <mutex>

#include "Log.h"
#include "deskhubp/Clock.h"

@class DeskhubStreamOutput;

// ---------------------------------------------------------------------------
// Trạng thái C++ của một stream
// ---------------------------------------------------------------------------
struct ScreenCapture::Impl {
    CaptureTarget target{};
    uint32_t fps = 60;
    FrameHandler onFrame;

    SCStream* stream = nil;
    SCContentFilter* filter = nil;
    SCStreamConfiguration* config = nil;
    DeskhubStreamOutput* output = nil;
    dispatch_queue_t frameQueue = nil;
    dispatch_source_t sizeTimer = nil;

    // Thang điểm→pixel của màn hình chứa nguồn, chụp lúc Start. Cửa sổ chuyển sang
    // màn hình khác scale giữa phiên là ca hiếm và tự sửa ở lần kéo cỡ kế tiếp.
    double scale = 1.0;

    // Cỡ buffer đang cấu hình. Chỉ bộ theo dõi cỡ (một queue duy nhất) chạm.
    uint32_t curW = 0, curH = 0;

    std::atomic<bool> closed{false};
    std::atomic<bool> running{false};
};

// ---------------------------------------------------------------------------
// Đối tượng Obj-C nhận frame
// ---------------------------------------------------------------------------
@interface DeskhubStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic, assign) ScreenCapture::Impl* impl;
@end

@implementation DeskhubStreamOutput

- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type != SCStreamOutputTypeScreen) return;
    ScreenCapture::Impl* impl = self.impl;
    if (!impl || !impl->running.load(std::memory_order_acquire)) return;
    if (!CMSampleBufferIsValid(sampleBuffer)) return;

    // SCFrameStatus: chỉ Complete là frame có nội dung MỚI. Idle nghĩa là "màn hình
    // không đổi" — bỏ qua, đúng như WGC không bắn FrameArrived khi nội dung đứng im.
    // Nén lại một frame y hệt chỉ tốn bitrate mà không cho người xem thêm gì.
    NSArray* attachments =
        (__bridge NSArray*)CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, NO);
    NSDictionary* info = attachments.count ? attachments[0] : nil;
    if (info) {
        NSNumber* st = info[SCStreamFrameInfoStatus];
        if (st && SCFrameStatus(st.integerValue) != SCFrameStatusComplete) return;
    }

    CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pb) return;

    MacFrameInfo fi;
    fi.pixelBuffer = (void*)pb;
    fi.width = uint32_t(CVPixelBufferGetWidth(pb));
    fi.height = uint32_t(CVPixelBufferGetHeight(pb));
    // Đồng hồ CỦA TA, không phải PTS của CMSampleBuffer: mọi phép đo thời gian trong
    // dự án (timeout phiên, RTT, e2e) đều dựa trên NowUs(), và trộn hai gốc thời
    // gian vào cùng một trường timestampUs trên wire là nguồn sai số không lần ra được.
    fi.timestampUs = NowUs();
    if (fi.width && fi.height && impl->onFrame) impl->onFrame(fi);
}

// SCStream chết hẳn (nguồn biến mất, người dùng thu hồi quyền giữa chừng, lỗi hệ
// thống). Không có đường hồi phục tại chỗ — báo closed để AgentLoop gỡ nguồn này.
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    (void)stream;
    LOGW("[Capture] Stream stopped: %s",
        error ? error.localizedDescription.UTF8String : "no error");
    if (self.impl) self.impl->closed.store(true, std::memory_order_release);
}

@end

namespace {

// Làm tròn XUỐNG số chẵn. H.264 lấy mẫu chroma theo khối 2×2 nên bề rộng/cao lẻ bị
// VideoToolbox từ chối; cắt một điểm ảnh rẻ hơn nhiều so với đệm thêm.
uint32_t Even(double v) {
    if (v < 2) return 0;
    return uint32_t(v) & ~1u;
}

// Cỡ PIXEL hiện tại của nguồn, hoặc {0,0} nếu nguồn đã biến mất.
//
// Dùng CGWindowListCopyWindowInfo chứ không hỏi lại SCShareableContent: hàm này rẻ
// (đọc thẳng từ WindowServer, không bất đồng bộ) nên gọi được mỗi 500ms, còn
// SCShareableContent thì không.
bool CurrentSourceSize(const CaptureTarget& t, double scale, uint32_t& w, uint32_t& h) {
    if (t.isDisplay) {
        const CGDirectDisplayID did = CGDirectDisplayID(t.id);
        const size_t pw = CGDisplayPixelsWide(did), ph = CGDisplayPixelsHigh(did);
        if (!pw || !ph) return false; // màn hình bị rút
        // CGDisplayPixelsWide trả về ĐIỂM trên màn Retina (nó là API đời cũ), nên
        // vẫn phải nhân scale — cùng phép tính với nhánh cửa sổ.
        w = Even(double(pw) * scale);
        h = Even(double(ph) * scale);
        return w && h;
    }

    const CGWindowID wid = CGWindowID(t.id);
    CFArrayRef arr = CGWindowListCopyWindowInfo(
        kCGWindowListOptionIncludingWindow | kCGWindowListExcludeDesktopElements, wid);
    if (!arr) return false;
    bool ok = false;
    if (CFArrayGetCount(arr) > 0) {
        NSDictionary* d = (__bridge NSDictionary*)CFArrayGetValueAtIndex(arr, 0);
        CGRect r = CGRectZero;
        if (CGRectMakeWithDictionaryRepresentation(
                (__bridge CFDictionaryRef)d[(__bridge NSString*)kCGWindowBounds], &r)) {
            w = Even(r.size.width * scale);
            h = Even(r.size.height * scale);
            ok = w && h;
        }
    }
    CFRelease(arr);
    return ok;
}

// Thang điểm→pixel của màn hình chứa `frame` (xem SourceEnum.mm về cùng phép tính).
double ScaleForFrame(CGRect frame) {
    double bestArea = 0;
    double scale = [NSScreen mainScreen] ? double([NSScreen mainScreen].backingScaleFactor) : 1.0;
    for (NSScreen* s in [NSScreen screens]) {
        const CGRect inter = CGRectIntersection(frame, s.frame);
        if (CGRectIsNull(inter)) continue;
        const double area = inter.size.width * inter.size.height;
        if (area > bestArea) {
            bestArea = area;
            scale = double(s.backingScaleFactor);
        }
    }
    return scale;
}

SCStreamConfiguration* MakeConfig(uint32_t w, uint32_t h, uint32_t fps) {
    SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
    cfg.width = w;
    cfg.height = h;
    // '420v' = NV12 dải video. VideoToolbox nhận thẳng, không phải đổi màu — xem
    // quyết định 3 ở ScreenCapture.h.
    cfg.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    // Trần fps. SCStream chỉ phát khi nội dung đổi nên đây là TRẦN chứ không phải
    // nhịp cố định — màn hình đứng im vẫn không tốn gì.
    cfg.minimumFrameInterval = CMTimeMake(1, int32_t(fps));
    // Người điều khiển từ xa cần thấy con trỏ của máy host để biết mình đang trỏ đâu.
    cfg.showsCursor = YES;
    // Hàng đợi frame của SCStream. Sâu quá thì frame cũ dồn lại làm tăng độ trễ,
    // nhưng 5 là con số tối thiểu ở đây vì AgentLoop GIỮ LẠI một buffer làm cache
    // frame cuối (nguồn tĩnh mà client xin IDR thì phải có cái để nén — xem
    // AgentLoop.cpp), và VideoToolbox giữ thêm một cái trong lúc nén. Đặt 3 thì hai
    // chỗ đó ăn hết pool và SCStream bắt đầu bỏ frame.
    cfg.queueDepth = 5;
    cfg.capturesAudio = NO;
    // Không co giãn: ta đã đặt cỡ buffer ĐÚNG bằng cỡ nguồn, nên mọi phép co giãn
    // đều là dấu hiệu bộ theo dõi cỡ đang trễ một nhịp, không phải điều ta muốn.
    cfg.scalesToFit = NO;
    return cfg;
}

} // namespace

ScreenCapture::ScreenCapture() : impl_(std::make_unique<Impl>()) {}

ScreenCapture::~ScreenCapture() {
    Stop();
}

bool ScreenCapture::Closed() const {
    return impl_->closed.load(std::memory_order_acquire);
}

bool ScreenCapture::Start(const CaptureTarget& target, uint32_t fps, FrameHandler onFrame) {
    if (!target.valid()) return false;
    Stop();

    impl_->target = target;
    impl_->fps = fps ? fps : 60;
    impl_->onFrame = std::move(onFrame);
    impl_->closed.store(false, std::memory_order_release);

    // --- Tìm đối tượng SCWindow/SCDisplay tương ứng với id ---
    // Phải qua SCShareableContent: SCContentFilter chỉ nhận đối tượng của nó, không
    // nhận id trần. Chờ đồng bộ như GetShareSources (xem SourceEnum.mm).
    __block SCShareableContent* content = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent* c, NSError* err) {
                                                   if (err)
                                                       LOGE("[Capture] SCShareableContent failed: %s",
                                                           err.localizedDescription.UTF8String);
                                                   content = c;
                                                   dispatch_semaphore_signal(sem);
                                                 }];
    if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC)) != 0) {
        LOGE("[Capture] SCShareableContent timed out (2s).");
        return false;
    }
    if (!content) return false;

    CGRect srcFrame = CGRectZero;
    if (target.isDisplay) {
        SCDisplay* found = nil;
        for (SCDisplay* d in content.displays)
            if (uint32_t(d.displayID) == target.id) found = d;
        if (!found) {
            LOGE("[Capture] Display %u not found.", target.id);
            return false;
        }
        srcFrame = found.frame;
        // excludingWindows rỗng: chia sẻ cả màn hình nghĩa là đúng những gì người
        // ngồi ở máy đang nhìn thấy — kể cả cửa sổ Deskhub. Che nó đi thì người dùng
        // không thấy được cửa sổ phiên của chính mình trong luồng, dễ tưởng treo.
        impl_->filter = [[SCContentFilter alloc] initWithDisplay:found
                                                excludingWindows:@[]];
    } else {
        SCWindow* found = nil;
        for (SCWindow* w in content.windows)
            if (uint32_t(w.windowID) == target.id) found = w;
        if (!found) {
            LOGE("[Capture] Window %u not found (closed?).", target.id);
            return false;
        }
        srcFrame = found.frame;
        impl_->filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:found];
    }

    impl_->scale = ScaleForFrame(srcFrame);

    uint32_t w = 0, h = 0;
    if (!CurrentSourceSize(target, impl_->scale, w, h)) {
        // Lùi về cỡ lấy từ đối tượng vừa tìm được: CGWindowList có thể chưa thấy cửa
        // sổ vừa mở, nhưng SCShareableContent thì vừa trả về nó nên chắc chắn có cỡ.
        w = Even(srcFrame.size.width * impl_->scale);
        h = Even(srcFrame.size.height * impl_->scale);
    }
    if (!w || !h) {
        LOGE("[Capture] Source %u has no usable size.", target.id);
        return false;
    }
    impl_->curW = w;
    impl_->curH = h;
    impl_->config = MakeConfig(w, h, impl_->fps);

    // --- Dựng stream ---
    impl_->output = [[DeskhubStreamOutput alloc] init];
    impl_->output.impl = impl_.get();
    impl_->stream = [[SCStream alloc] initWithFilter:impl_->filter
                                       configuration:impl_->config
                                            delegate:impl_->output];

    // Queue nối tiếp riêng cho frame: nó chính là "thread FrameArrived" của bản
    // Windows — mỗi nguồn một queue, và mọi thứ chạy trên nó (encode, packetize)
    // được bảo đảm nối tiếp, không cần khoá thêm.
    impl_->frameQueue = dispatch_queue_create("com.manhpham.deskhub.capture",
        dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,
            QOS_CLASS_USER_INTERACTIVE, 0));

    NSError* err = nil;
    if (![impl_->stream addStreamOutput:impl_->output
                                   type:SCStreamOutputTypeScreen
                     sampleHandlerQueue:impl_->frameQueue
                                  error:&err]) {
        LOGE("[Capture] addStreamOutput failed: %s",
            err ? err.localizedDescription.UTF8String : "?");
        Stop();
        return false;
    }

    __block bool started = false;
    dispatch_semaphore_t startSem = dispatch_semaphore_create(0);
    [impl_->stream startCaptureWithCompletionHandler:^(NSError* e) {
      if (e)
          LOGE("[Capture] startCapture failed: %s", e.localizedDescription.UTF8String);
      started = (e == nil);
      dispatch_semaphore_signal(startSem);
    }];
    if (dispatch_semaphore_wait(startSem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) != 0) {
        LOGE("[Capture] startCapture timed out (5s).");
        Stop();
        return false;
    }
    if (!started) {
        Stop();
        return false;
    }
    impl_->running.store(true, std::memory_order_release);

    // --- Bộ theo dõi cỡ + phát hiện nguồn biến mất (xem đầu file) ---
    Impl* impl = impl_.get();
    impl_->sizeTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
        dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    dispatch_source_set_timer(impl_->sizeTimer,
        dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC),
        500 * NSEC_PER_MSEC, 100 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(impl_->sizeTimer, ^{
      if (!impl->running.load(std::memory_order_acquire)) return;
      uint32_t nw = 0, nh = 0;
      if (!CurrentSourceSize(impl->target, impl->scale, nw, nh)) {
          LOGI("[Capture] Source %u disappeared.", impl->target.id);
          impl->closed.store(true, std::memory_order_release);
          return;
      }
      if (nw == impl->curW && nh == impl->curH) return;
      LOGI("[Capture] Source resized %ux%u -> %ux%u, reconfiguring stream.",
          impl->curW, impl->curH, nw, nh);
      impl->curW = nw;
      impl->curH = nh;
      SCStreamConfiguration* cfg = MakeConfig(nw, nh, impl->fps);
      impl->config = cfg;
      [impl->stream updateConfiguration:cfg
                      completionHandler:^(NSError* e) {
                        if (e)
                            LOGW("[Capture] updateConfiguration failed: %s",
                                e.localizedDescription.UTF8String);
                      }];
    });
    dispatch_resume(impl_->sizeTimer);

    LOGI("[Capture] Capturing %s %u at %ux%u (scale %.1f), max %u fps.",
        target.isDisplay ? "display" : "window", target.id, w, h, impl_->scale, impl_->fps);
    return true;
}

// Dừng theo thứ tự NGƯỢC với Start, và tắt cờ running TRƯỚC mọi thứ khác: callback
// frame lẫn bộ theo dõi cỡ đều kiểm tra cờ đó, nên tắt nó là đóng cửa hai đường vào
// impl_ trước khi ta bắt đầu tháo dỡ những gì chúng đọc.
void ScreenCapture::Stop() {
    if (!impl_) return;
    impl_->running.store(false, std::memory_order_release);

    if (impl_->sizeTimer) {
        dispatch_source_cancel(impl_->sizeTimer);
        impl_->sizeTimer = nil;
    }

    if (impl_->stream) {
        // Chờ stopCapture xong hẳn: nó bảo đảm không còn callback nào đang chạy khi
        // trả về. Không chờ thì một frame đang bay có thể chạm vào onFrame — mà
        // AgentLoop đã dọn encoder ngay sau lời gọi Stop này.
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [impl_->stream stopCaptureWithCompletionHandler:^(NSError* e) {
          (void)e;
          dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
        impl_->stream = nil;
    }

    if (impl_->output) {
        impl_->output.impl = nullptr;
        impl_->output = nil;
    }
    impl_->filter = nil;
    impl_->config = nil;
    impl_->frameQueue = nil;
    impl_->onFrame = nullptr;
}
