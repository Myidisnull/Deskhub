// =============================================================================
// InputInjector.mm — cài đặt bằng Quartz Event Services (CGEvent).
//
// BỐ CỤC
//   SourceRect()      — rect màn hình theo ĐIỂM, toàn cục (có cache 200ms).
//   Apply()           — cổng vào duy nhất, gác hai cơ chế an toàn rồi phân loại event.
//   Send*()           — dựng và bắn CGEvent cho từng loại.
//
// MỌI SỰ KIỆN BẮN RA ĐỀU MANG DẤU kUserData
//   Bắt buộc, không phải tuỳ chọn: LocalInputMonitor lọc theo dấu đó để không nhầm
//   input ta bơm với input của người ngồi máy. Quên đóng dấu ở MỘT đường bắn thôi là
//   kênh điều khiển tự khoá chính nó — xem input/LocalInputMonitor.h.
//
// VÌ SAO POST VÀO kCGHIDEventTap
//   Đó là điểm sớm nhất trong đường ống sự kiện, trước cả các bộ lọc của hệ thống —
//   nên sự kiện đi được vào mọi ứng dụng, kể cả game đọc HID ở tầng thấp. Bắn vào
//   kCGSessionEventTap thì một số ứng dụng toàn màn hình không thấy gì.
//
// LIÊN QUAN: input/InputInjector.h (hai cơ chế an toàn + ánh xạ toạ độ),
//            input/MacKeyMap.h (VK → keycode), input/LocalInputMonitor.h
// =============================================================================
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include "input/InputInjector.h"

#include <cmath>

#include "Log.h"
#include "input/LocalInputMonitor.h"
#include "Permissions.h"
#include "deskhubp/Clock.h"
#include "input/MacKeyMap.h"

namespace {

// Rect màn hình được hỏi lại nhiều nhất mỗi ngần này. Chuột di chuyển sinh hàng
// trăm event mỗi giây còn cấu hình màn hình thì hiếm khi đổi — hỏi mỗi event là
// lãng phí thấy rõ, mà trễ 200ms sau khi đổi độ phân giải thì không ai nhận ra.
constexpr uint64_t kRectCacheUs = 200'000;

// Hai cú click tính là double-click khi cách nhau dưới ngần này VÀ con trỏ gần như
// không dịch. 500ms là mặc định của macOS (NSEvent.doubleClickInterval đọc được cấu
// hình thật của người dùng, nhưng nó là API AppKit chỉ gọi an toàn trên main thread,
// còn hàm này chạy trên thread Recv).
constexpr uint64_t kDoubleClickUs = 500'000;
constexpr double kDoubleClickSlopPt = 4.0;

CGEventType MoveTypeFor(const std::set<deskhub::MouseButton>& down) {
    if (down.count(deskhub::MouseButton::Left)) return kCGEventLeftMouseDragged;
    if (down.count(deskhub::MouseButton::Right)) return kCGEventRightMouseDragged;
    if (!down.empty()) return kCGEventOtherMouseDragged;
    return kCGEventMouseMoved;
}

// deskhub::MouseButton -> (loại event down/up, chỉ số nút của Quartz).
bool ButtonCodes(deskhub::MouseButton b, CGEventType& downType, CGEventType& upType,
    CGMouseButton& which) {
    switch (b) {
        case deskhub::MouseButton::Left:
            downType = kCGEventLeftMouseDown;
            upType = kCGEventLeftMouseUp;
            which = kCGMouseButtonLeft;
            return true;
        case deskhub::MouseButton::Right:
            downType = kCGEventRightMouseDown;
            upType = kCGEventRightMouseUp;
            which = kCGMouseButtonRight;
            return true;
        case deskhub::MouseButton::Middle:
            downType = kCGEventOtherMouseDown;
            upType = kCGEventOtherMouseUp;
            which = kCGMouseButtonCenter;
            return true;
        // X1/X2: Quartz đánh số nút phụ tiếp sau nút giữa. Ít ứng dụng macOS đọc
        // chúng, nhưng bắn ra vẫn hơn là nuốt im lặng.
        case deskhub::MouseButton::X1:
            downType = kCGEventOtherMouseDown;
            upType = kCGEventOtherMouseUp;
            which = CGMouseButton(3);
            return true;
        case deskhub::MouseButton::X2:
            downType = kCGEventOtherMouseDown;
            upType = kCGEventOtherMouseUp;
            which = CGMouseButton(4);
            return true;
    }
    return false;
}

// Vị trí con trỏ hiện tại theo ĐIỂM, hệ toạ độ toàn cục gốc trên-trái — cùng hệ với
// CGEvent. (NSEvent.mouseLocation dùng gốc DƯỚI-trái, lệch trục Y; đừng trộn hai cái.)
CGPoint CursorPoint() {
    CGEventRef probe = CGEventCreate(nullptr);
    const CGPoint p = probe ? CGEventGetLocation(probe) : CGPointZero;
    if (probe) CFRelease(probe);
    return p;
}

// Giới hạn toàn bộ vùng màn hình (hợp của mọi màn hình), để chuột tương đối không
// chạy ra hư không.
CGRect DesktopBounds() {
    CGRect all = CGRectNull;
    for (NSScreen* s in [NSScreen screens])
        all = CGRectIsNull(all) ? s.frame : CGRectUnion(all, s.frame);
    return CGRectIsNull(all) ? CGRectMake(0, 0, 1920, 1080) : all;
}

} // namespace

InputInjector::InputInjector() {
    // kCGEventSourceStateHIDSystemState: sự kiện ta bắn ra HOÀ vào trạng thái phần
    // cứng thật. Quan trọng cho phím bổ trợ — chủ máy đang giữ Shift thật thì ký tự
    // từ xa cũng thành chữ hoa, đúng như hai người dùng chung một bàn phím.
    source_ = (void*)CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!source_) LOGW("[Input] CGEventSourceCreate failed — using default source.");
}

InputInjector::~InputInjector() {
    ReleaseAll();
    if (source_) {
        CFRelease((CGEventSourceRef)source_);
        source_ = nullptr;
    }
}

bool InputInjector::Init(uint32_t displayId) {
    if (!displayId) return false;
    displayId_ = displayId;
    rectUs_ = 0; // buộc lần SourceRect kế tiếp hỏi lại thật

    if (!macperm::HasAccessibility()) {
        // KHÔNG trả false: chia sẻ vẫn chạy được ở chế độ chỉ-xem, và người dùng có
        // thể bật quyền giữa phiên (Accessibility có hiệu lực ngay, không cần khởi
        // động lại app). Nói thẳng ra log là đủ — caller quyết định có bật input hay
        // không, và UI có nút mở System Settings.
        LOGW("[Input] Accessibility permission missing — injected input will be "
             "silently dropped by macOS until it is granted.");
    }

    double x, y, w, h;
    if (!SourceRect(x, y, w, h)) {
        LOGE("[Input] Display %u has no bounds — input disabled for it.", displayId);
        return false;
    }
    return true;
}

// Rect màn hình theo ĐIỂM, gốc trên-trái toàn cục. Cache kRectCacheUs.
bool InputInjector::SourceRect(double& x, double& y, double& w, double& h) {
    const uint64_t now = NowUs();
    if (rectUs_ && now - rectUs_ < kRectCacheUs && rectW_ > 0) {
        x = rectX_;
        y = rectY_;
        w = rectW_;
        h = rectH_;
        return true;
    }

    const CGRect r = CGDisplayBounds(CGDirectDisplayID(displayId_));
    if (r.size.width <= 0) return false; // màn hình bị rút
    rectX_ = r.origin.x;
    rectY_ = r.origin.y;
    rectW_ = r.size.width;
    rectH_ = r.size.height;

    rectUs_ = now;
    x = rectX_;
    y = rectY_;
    w = rectW_;
    h = rectH_;
    return true;
}

void InputInjector::SetEnabled(bool on) {
    if (enabled_ == on) return;
    enabled_ = on;
    if (!on) ReleaseAll();
}

uint64_t InputInjector::CurrentFlags() const {
    CGEventFlags f = 0;
    for (int32_t vk : modsDown_) {
        switch (mackeys::ModifierOf(vk)) {
            case mackeys::Modifier::Shift: f |= kCGEventFlagMaskShift; break;
            case mackeys::Modifier::Control: f |= kCGEventFlagMaskControl; break;
            case mackeys::Modifier::Option: f |= kCGEventFlagMaskAlternate; break;
            case mackeys::Modifier::Command: f |= kCGEventFlagMaskCommand; break;
            case mackeys::Modifier::CapsLock: f |= kCGEventFlagMaskAlphaShift; break;
            case mackeys::Modifier::None: break;
        }
    }
    return uint64_t(f);
}

// Cổng vào duy nhất. Hai lớp gác trước khi bơm bất cứ thứ gì:
//   1. Bật/tắt (người chia sẻ chọn chế độ chỉ-xem).
//   2. Host thắng (người ngồi máy vừa dùng chuột/phím thật).
void InputInjector::Apply(const deskhub::InputEvent& e) {
    if (!enabled_) return;

    if (localMon_ && localMon_->LocalActive(NowUs())) {
        if (!localSuppressed_) {
            localSuppressed_ = true;
            LOGI("[Input] Someone is using this Mac — remote input paused.");
        }
        // Nhả những gì đang giữ TRƯỚC khi nhường: không nhả thì phím W từ xa kẹt lại
        // trong suốt lúc chủ máy đang gõ.
        ReleaseAll();
        ++skipped_;
        return;
    }
    if (localSuppressed_) {
        localSuppressed_ = false;
        LOGI("[Input] Remote input resumed.");
    }

    switch (e.type) {
        case deskhub::InputType::Key:
            SendKey(e.a, e.state != 0);
            break;
        case deskhub::InputType::MouseMove:
            if (e.absolute)
                SendMoveAbsolute(e.a, e.b);
            else
                SendMoveRelative(e.a, e.b);
            break;
        case deskhub::InputType::MouseButton:
            SendButton(deskhub::MouseButton(e.a), e.state != 0);
            break;
        case deskhub::InputType::MouseWheel:
            SendWheel(e.b);
            break;
    }
    ++applied_;
}

void InputInjector::SendKey(int32_t vk, bool down) {
    uint16_t keycode = 0;
    if (!mackeys::WinVkToMac(vk, keycode)) return; // phím không có trên Mac — bỏ qua

    // Ghi sổ TRƯỚC khi dựng event: CurrentFlags() phải đã thấy phím bổ trợ vừa nhấn,
    // nếu không thì chính sự kiện Shift↓ lại thiếu cờ Shift và vài ứng dụng bỏ qua nó.
    const mackeys::Modifier mod = mackeys::ModifierOf(vk);
    if (down) {
        keysDown_[vk] = keycode;
        if (mod != mackeys::Modifier::None) modsDown_.insert(vk);
    } else {
        keysDown_.erase(vk);
        if (mod != mackeys::Modifier::None) modsDown_.erase(vk);
    }

    CGEventRef ev = CGEventCreateKeyboardEvent((CGEventSourceRef)source_,
        CGKeyCode(keycode), down);
    if (!ev) return;
    CGEventSetFlags(ev, CGEventFlags(CurrentFlags()));
    CGEventSetIntegerValueField(ev, kCGEventSourceUserData, LocalInputMonitor::kUserData);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

void InputInjector::PostMouseAt(double x, double y, int32_t dx, int32_t dy) {
    const CGEventType type = MoveTypeFor(buttonsDown_);
    // Nút "đang kéo" phải đi kèm event drag, nếu không ứng dụng không biết kéo bằng
    // nút nào.
    CGMouseButton which = kCGMouseButtonLeft;
    if (buttonsDown_.count(deskhub::MouseButton::Right))
        which = kCGMouseButtonRight;
    else if (buttonsDown_.count(deskhub::MouseButton::Middle))
        which = kCGMouseButtonCenter;

    CGEventRef ev = CGEventCreateMouseEvent((CGEventSourceRef)source_, type,
        CGPointMake(x, y), which);
    if (!ev) return;
    // Delta THÔ: game FPS đọc trường này chứ không đọc hiệu hai vị trí liên tiếp —
    // đúng lý do client có chế độ chuột tương đối (docs/07 §5). Chế độ tuyệt đối thì
    // dx/dy = 0 và không ai đọc tới.
    CGEventSetIntegerValueField(ev, kCGMouseEventDeltaX, dx);
    CGEventSetIntegerValueField(ev, kCGMouseEventDeltaY, dy);
    CGEventSetFlags(ev, CGEventFlags(CurrentFlags()));
    CGEventSetIntegerValueField(ev, kCGEventSourceUserData, LocalInputMonitor::kUserData);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

void InputInjector::SendMoveAbsolute(int32_t nx, int32_t ny) {
    double rx, ry, rw, rh;
    if (!SourceRect(rx, ry, rw, rh)) return;
    // 0..65535 -> điểm trong rect nguồn. Chia cho 65535.0 chứ không 65536: client
    // chuẩn hoá sao cho cạnh phải/dưới đạt ĐÚNG 65535 (xem Normalize bên
    // InputCapture), nên mẫu số phải khớp, không thì không bao giờ chạm được mép.
    const double x = rx + double(nx) / 65535.0 * rw;
    const double y = ry + double(ny) / 65535.0 * rh;
    PostMouseAt(x, y, 0, 0);
}

void InputInjector::SendMoveRelative(int32_t dx, int32_t dy) {
    const CGPoint cur = CursorPoint();
    const CGRect bounds = DesktopBounds();
    // Kẹp vào vùng màn hình: delta dồn mãi một hướng (người chơi xoay người liên
    // tục) sẽ đẩy con trỏ ra ngoài mọi màn hình và mọi click sau đó rơi vào hư không.
    double x = cur.x + double(dx);
    double y = cur.y + double(dy);
    x = std::fmin(std::fmax(x, CGRectGetMinX(bounds)), CGRectGetMaxX(bounds) - 1);
    y = std::fmin(std::fmax(y, CGRectGetMinY(bounds)), CGRectGetMaxY(bounds) - 1);
    PostMouseAt(x, y, dx, dy);
}

void InputInjector::SendButton(deskhub::MouseButton btn, bool down) {
    CGEventType downType, upType;
    CGMouseButton which;
    if (!ButtonCodes(btn, downType, upType, which)) return;

    const CGPoint p = CursorPoint();

    // Đếm click liên tiếp — xem chú thích clickState_ ở header.
    if (down) {
        const uint64_t now = NowUs();
        const bool near = std::fabs(p.x - lastClickX_) <= kDoubleClickSlopPt &&
                          std::fabs(p.y - lastClickY_) <= kDoubleClickSlopPt;
        if (btn == lastClickBtn_ && near && lastClickUs_ && now - lastClickUs_ <= kDoubleClickUs)
            ++clickState_;
        else
            clickState_ = 1;
        lastClickUs_ = now;
        lastClickX_ = p.x;
        lastClickY_ = p.y;
        lastClickBtn_ = btn;
        buttonsDown_.insert(btn);
    } else {
        buttonsDown_.erase(btn);
    }

    CGEventRef ev = CGEventCreateMouseEvent((CGEventSourceRef)source_,
        down ? downType : upType, p, which);
    if (!ev) return;
    CGEventSetIntegerValueField(ev, kCGMouseEventClickState, clickState_);
    CGEventSetFlags(ev, CGEventFlags(CurrentFlags()));
    CGEventSetIntegerValueField(ev, kCGEventSourceUserData, LocalInputMonitor::kUserData);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

void InputInjector::SendWheel(int32_t delta) {
    if (!delta) return;
    // Giao thức mang delta theo bội của 120 (WHEEL_DELTA của Windows); Quartz đếm
    // theo DÒNG. 120 = một nấc = 3 dòng, đúng mặc định của Windows — cuộn từ xa vì
    // thế cho cảm giác giống hệt cuộn tại chỗ.
    const int32_t lines = (delta / 120) * 3;
    if (!lines) return;
    CGEventRef ev = CGEventCreateScrollWheelEvent((CGEventSourceRef)source_,
        kCGScrollEventUnitLine, 1, lines);
    if (!ev) return;
    CGEventSetFlags(ev, CGEventFlags(CurrentFlags()));
    CGEventSetIntegerValueField(ev, kCGEventSourceUserData, LocalInputMonitor::kUserData);
    CGEventPost(kCGHIDEventTap, ev);
    CFRelease(ev);
}

// Nhả mọi thứ đang giữ. Duyệt trên BẢN SAO rồi xoá sổ: SendKey/SendButton tự sửa
// keysDown_/buttonsDown_, nên vừa duyệt vừa gọi chúng là hỏng iterator.
void InputInjector::ReleaseAll() {
    if (!keysDown_.empty()) {
        const auto keys = keysDown_;
        for (const auto& [vk, keycode] : keys) SendKey(vk, false);
    }
    if (!buttonsDown_.empty()) {
        const auto btns = buttonsDown_;
        for (deskhub::MouseButton b : btns) SendButton(b, false);
    }
    keysDown_.clear();
    buttonsDown_.clear();
    modsDown_.clear();
}
