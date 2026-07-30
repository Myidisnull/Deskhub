// =============================================================================
// ScreenCapture.mm — cài đặt bằng SCStream.
//
// BỐ CỤC
//   DeskhubStreamOutput — đối tượng Obj-C nhận frame (SCStreamOutput) và nhận tin
//                         stream chết (SCStreamDelegate). Giữ con trỏ về Impl.
//   Impl                — trạng thái C++: stream, cấu hình hiện tại, bộ theo dõi cỡ.
//   Start/Stop/Closed   — vòng đời, khớp API của ScreenCapture bên Windows.
//
// VÌ SAO CÓ BỘ THEO DÕI CỠ (dispatch timer 500ms)
//   SCStreamConfiguration.width/height là cỡ BUFFER, cố định lúc tạo stream. Đổi
//   độ phân giải/scale màn hình thì SCStream vẫn giao buffer cũ với nội dung bị co
//   lại — không có sự kiện nào báo. Bộ đếm này so cỡ nguồn với cỡ buffer, lệch thì
//   gọi updateConfiguration; frame kế tiếp về đúng cỡ mới và AgentLoop nhận ra qua
//   đường "sizeChanged" y hệt bản Windows.
//
//   Nó cũng là nơi phát hiện MÀN HÌNH BỊ RÚT: CGDisplayPixelsWide trả 0 cho một
//   displayID không còn tồn tại. SCStream không báo gì khi nguồn biến mất — nó chỉ
//   ngừng phát frame, và một nguồn im lặng vĩnh viễn nhìn y như mạng hỏng.
//
// VÌ SAO 500ms CHỨ KHÔNG NHANH HƠN
//   Đổi độ phân giải là thao tác hiếm của con người, không phải sự kiện realtime.
//   Mỗi lần đổi cấu hình là một lần encoder phía trên phải dựng lại và phát IDR —
//   đắt. 500ms đủ nhanh với người dùng mà không dựng lại encoder hàng chục lần.
//
// LIÊN QUAN: capture/ScreenCapture.h (bốn quyết định thiết kế)
// =============================================================================
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include "capture/ScreenCapture.h"

#include <atomic>
#include <mutex>

#include "Log.h"
#include "deskhub/control/StreamSize.h"
#include "deskhubp/Clock.h"

@class DeskhubStreamOutput;

// ---------------------------------------------------------------------------
// Trạng thái C++ của một stream
// ---------------------------------------------------------------------------
struct ScreenCapture::Impl {
    uint32_t displayId = 0; // CGDirectDisplayID
    uint32_t fps = 60;
    uint32_t maxDim = 0; // trần cạnh dài, 0 = native (quyết định 5 ở .h)
    FrameHandler onFrame;

    // Cỡ màn hình client, từ HELLO. 0 = chưa có client / client không nói.
    uint32_t cliW = 0, cliH = 0;

    SCStream* stream = nil;
    SCContentFilter* filter = nil;
    SCStreamConfiguration* config = nil;
    DeskhubStreamOutput* output = nil;
    dispatch_queue_t frameQueue = nil;
    dispatch_source_t sizeTimer = nil;

    // Thang điểm→pixel của màn hình, chụp lúc Start. Đổi scale giữa phiên tự sửa
    // ở nhịp theo dõi cỡ kế tiếp.
    double scale = 1.0;

    // Cỡ NGUỒN đang biết (native, chưa co) và cỡ BUFFER đang cấu hình (đã co theo
    // maxDim + cỡ màn client). Phải giữ cả hai: bộ theo dõi cỡ so với cỡ NGUỒN để
    // biết màn hình có đổi độ phân giải không, còn updateConfiguration thì cần cỡ
    // BUFFER. Gộp một biến thì màn 3024×1964 co xuống 1920×1246 sẽ trông như "vừa
    // đổi cỡ" ở mỗi nhịp 500ms và stream bị dựng lại vĩnh viễn.
    //
    // sizeMutex bảo vệ SÁU trường trên cùng lời gọi updateConfiguration. Trước đây
    // chỉ bộ theo dõi cỡ (một queue duy nhất) chạm nên không cần khoá; từ khi
    // SetClientSize được gọi từ thread Recv lúc HELLO thì có hai luồng, và hai lời
    // gọi updateConfiguration đan nhau sẽ để lại cfgW/cfgH không khớp cấu hình thật.
    std::mutex sizeMutex;
    uint32_t curW = 0, curH = 0; // nguồn
    uint32_t cfgW = 0, cfgH = 0; // buffer
    // Bậc chất lượng hiện tại (deskhub::QualityLadder), áp SAU hai trần kia. Giữ dạng
    // PHẦN TRĂM chứ không phải cỡ tuyệt đối: trần đổi giữa phiên (người dùng đổi độ
    // phân giải màn hình nguồn) thì "50% của trần hiện tại" vẫn đúng, còn một cặp
    // w/h chốt cứng thì thành số cũ. Xem QualityStep::scalePct.
    uint32_t qualityPct = 100;

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

// Cỡ PIXEL hiện tại của màn hình, hoặc {0,0} nếu nó đã bị rút. Rẻ (đọc thẳng từ
// WindowServer, không bất đồng bộ) nên gọi được mỗi 500ms.
bool CurrentSourceSize(uint32_t displayId, double scale, uint32_t& w, uint32_t& h) {
    const CGDirectDisplayID did = CGDirectDisplayID(displayId);
    const size_t pw = CGDisplayPixelsWide(did), ph = CGDisplayPixelsHigh(did);
    if (!pw || !ph) return false; // màn hình bị rút
    // CGDisplayPixelsWide trả về ĐIỂM trên màn Retina (nó là API đời cũ), nên
    // vẫn phải nhân scale.
    w = Even(double(pw) * scale);
    h = Even(double(ph) * scale);
    return w && h;
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

SCStreamConfiguration* MakeConfig(uint32_t w, uint32_t h, uint32_t fps, bool scaled) {
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
    // Chỉ bật co giãn khi ta CỐ Ý hạ độ phân giải (maxDim). Không có trần thì cỡ
    // buffer đúng bằng cỡ nguồn, và khi đó mọi phép co giãn đều là dấu hiệu bộ theo
    // dõi cỡ đang trễ một nhịp chứ không phải điều ta muốn.
    //
    // Phép co này chạy trong WindowServer, trên GPU, TRƯỚC khi frame tới tay ta —
    // rẻ hơn mọi cách hạ độ phân giải khác, và encoder không bao giờ nhìn thấy khung
    // to. Đây chính là điểm mấu chốt của quyết định 5.
    cfg.scalesToFit = scaled ? YES : NO;
    return cfg;
}

// Tính lại cỡ buffer từ (cỡ nguồn, trần người dùng, cỡ màn client) và cấu hình lại
// stream nếu nó đổi. GỌI DƯỚI impl->sizeMutex, với impl->curW/curH đã cập nhật.
//
// MỘT ĐƯỜNG DUY NHẤT cho cả hai người gọi (bộ theo dõi cỡ 500ms và SetClientSize từ
// HELLO): hai đường song song đồng nghĩa hai bản của cùng chính sách, và bản nào
// lệch thì lộ ra dưới dạng một cỡ khung sai chỉ xuất hiện khi người dùng vừa đổi độ
// phân giải vừa có client kết nối — đúng loại lỗi không ai tái hiện nổi.
//
// Trả về cỡ buffer sau khi tính (dù có đổi hay không).
deskhub::StreamSize ApplySizeLocked(ScreenCapture::Impl* impl) {
    deskhub::StreamSize t =
        deskhub::FitStreamSize(impl->curW, impl->curH, impl->maxDim, impl->cliW, impl->cliH);
    if (!t.width || !t.height) return {impl->cfgW, impl->cfgH};
    // Bậc chất lượng áp SAU CÙNG: hai trần trên trả lời "được phép gửi bao nhiêu
    // pixel", còn bậc này trả lời "đường truyền lúc này tải nổi bao nhiêu". Thứ tự
    // ngược lại sẽ để trần client cắt lại cỡ đã co và cho ra một cỡ không thuộc
    // thang nào cả.
    if (impl->qualityPct < 100) {
        t.width = (t.width * impl->qualityPct / 100u) & ~1u;
        t.height = (t.height * impl->qualityPct / 100u) & ~1u;
        if (!t.width || !t.height) return {impl->cfgW, impl->cfgH};
    }
    if (t.width == impl->cfgW && t.height == impl->cfgH) return t;

    impl->cfgW = t.width;
    impl->cfgH = t.height;
    SCStreamConfiguration* cfg = MakeConfig(t.width, t.height, impl->fps,
        t.width != impl->curW || t.height != impl->curH);
    impl->config = cfg;
    [impl->stream updateConfiguration:cfg
                    completionHandler:^(NSError* e) {
                      if (e)
                          LOGW("[Capture] updateConfiguration failed: %s",
                              e.localizedDescription.UTF8String);
                    }];
    return t;
}

} // namespace

ScreenCapture::ScreenCapture() : impl_(std::make_unique<Impl>()) {}

ScreenCapture::~ScreenCapture() {
    Stop();
}

bool ScreenCapture::Closed() const {
    return impl_->closed.load(std::memory_order_acquire);
}

bool ScreenCapture::Start(uint32_t displayId, uint32_t fps, uint32_t maxDim,
    FrameHandler onFrame) {
    if (!displayId) return false;
    Stop();

    impl_->displayId = displayId;
    impl_->fps = fps ? fps : 60;
    impl_->maxDim = maxDim;
    impl_->onFrame = std::move(onFrame);
    impl_->closed.store(false, std::memory_order_release);

    // --- Tìm đối tượng SCDisplay tương ứng với id ---
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

    SCDisplay* found = nil;
    for (SCDisplay* d in content.displays)
        if (uint32_t(d.displayID) == displayId) found = d;
    if (!found) {
        LOGE("[Capture] Display %u not found.", displayId);
        return false;
    }
    const CGRect srcFrame = found.frame;
    // excludingWindows rỗng: chia sẻ cả màn hình nghĩa là đúng những gì người
    // ngồi ở máy đang nhìn thấy — kể cả cửa sổ Deskhub. Che nó đi thì người dùng
    // không thấy được cửa sổ phiên của chính mình trong luồng, dễ tưởng treo.
    impl_->filter = [[SCContentFilter alloc] initWithDisplay:found
                                            excludingWindows:@[]];

    impl_->scale = ScaleForFrame(srcFrame);

    uint32_t w = 0, h = 0;
    if (!CurrentSourceSize(displayId, impl_->scale, w, h)) {
        // Lùi về cỡ lấy từ đối tượng vừa tìm được — SCShareableContent vừa trả về
        // nó nên chắc chắn có cỡ.
        w = Even(srcFrame.size.width * impl_->scale);
        h = Even(srcFrame.size.height * impl_->scale);
    }
    if (!w || !h) {
        LOGE("[Capture] Display %u has no usable size.", displayId);
        return false;
    }
    impl_->curW = w;
    impl_->curH = h;
    // Chưa có client ở đây, nên chỉ trần người dùng có hiệu lực. Cỡ được siết thêm
    // lần nữa ở SetClientSize khi client HELLO.
    const deskhub::StreamSize t0 = deskhub::FitStreamSize(w, h, impl_->maxDim, 0, 0);
    impl_->cfgW = t0.width;
    impl_->cfgH = t0.height;
    impl_->config = MakeConfig(t0.width, t0.height, impl_->fps,
        t0.width != w || t0.height != h);

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
      if (!CurrentSourceSize(impl->displayId, impl->scale, nw, nh)) {
          LOGI("[Capture] Display %u disappeared.", impl->displayId);
          impl->closed.store(true, std::memory_order_release);
          return;
      }
      std::lock_guard<std::mutex> lk(impl->sizeMutex);
      if (nw == impl->curW && nh == impl->curH) return;
      const uint32_t oldW = impl->curW, oldH = impl->curH;
      impl->curW = nw;
      impl->curH = nh;
      // ApplySizeLocked tự bỏ qua khi cỡ buffer không đổi (nguồn 3024→3200 mà trần
      // vẫn 1920 thì buffer y nguyên) — dựng lại encoder ở đó chỉ tốn một IDR vô ích.
      const deskhub::StreamSize t = ApplySizeLocked(impl);
      LOGI("[Capture] Source resized %ux%u -> %ux%u, streaming at %ux%u.", oldW, oldH, nw, nh,
          t.width, t.height);
    });
    dispatch_resume(impl_->sizeTimer);

    if (impl_->cfgW != w || impl_->cfgH != h)
        LOGI("[Capture] Capturing display %u: native %ux%u -> streaming %ux%u"
             " (cap %u px), max %u fps.",
            displayId, w, h, impl_->cfgW, impl_->cfgH, impl_->maxDim, impl_->fps);
    else
        LOGI("[Capture] Capturing display %u at %ux%u (scale %.1f), max %u fps.",
            displayId, w, h, impl_->scale, impl_->fps);
    return true;
}

void ScreenCapture::SetClientSize(uint32_t clientW, uint32_t clientH, uint32_t& outW,
    uint32_t& outH) {
    std::lock_guard<std::mutex> lk(impl_->sizeMutex);
    if (clientW != impl_->cliW || clientH != impl_->cliH) {
        impl_->cliW = clientW;
        impl_->cliH = clientH;
        const deskhub::StreamSize t = ApplySizeLocked(impl_.get());
        if (clientW && clientH)
            LOGI("[Capture] Client screen %ux%u -> streaming %ux%u (source %ux%u).", clientW,
                clientH, t.width, t.height, impl_->curW, impl_->curH);
    }
    outW = impl_->cfgW;
    outH = impl_->cfgH;
}

// Áp một bậc của thang chất lượng. Gọi từ thread Recv khi QualityLadder đổi bậc.
//
// Đi CHUNG một đường ApplySizeLocked với bộ theo dõi cỡ và SetClientSize, vì cùng
// một lý do đã ghi ở đó: ba đường song song là ba bản của cùng một chính sách, và
// bản nào lệch chỉ lộ ra khi cả ba cùng chạy — đúng loại lỗi không tái hiện nổi.
void ScreenCapture::SetQuality(uint32_t scalePct, uint32_t fps, uint32_t& outW,
    uint32_t& outH) {
    std::lock_guard<std::mutex> lk(impl_->sizeMutex);
    const uint32_t pct = scalePct ? (scalePct > 100 ? 100 : scalePct) : 100;
    const uint32_t f = fps ? fps : impl_->fps;
    if (pct != impl_->qualityPct || f != impl_->fps) {
        impl_->qualityPct = pct;
        impl_->fps = f;
        // Cỡ không đổi (bậc chỉ hạ fps) thì ApplySizeLocked thoát sớm và
        // minimumFrameInterval MỚI không bao giờ tới được SCStream. Ép cấu hình lại
        // bằng cách xoá cỡ đang nhớ — rẻ, vì đây là đường mỗi-vài-giây, không phải
        // đường nóng.
        impl_->cfgW = impl_->cfgH = 0;
        const deskhub::StreamSize t = ApplySizeLocked(impl_.get());
        LOGI("[Capture] Quality step: %u%% @%ufps -> streaming %ux%u.", pct, f, t.width,
            t.height);
    }
    outW = impl_->cfgW;
    outH = impl_->cfgH;
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
