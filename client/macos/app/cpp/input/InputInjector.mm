#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include "input/InputInjector.h"

#include <cmath>

#include "deskhubp/Log.h"
#include "input/LocalInputMonitor.h"
#include "Permissions.h"
#include "deskhubp/Clock.h"
#include "input/MacKeyMap.h"

namespace {

constexpr uint64_t kRectCacheUs = 200'000;

constexpr uint64_t kDoubleClickUs = 500'000;
constexpr double kDoubleClickSlopPt = 4.0;

CGEventType MoveTypeFor(const std::set<deskhub::MouseButton>& down) {
    if (down.count(deskhub::MouseButton::Left)) return kCGEventLeftMouseDragged;
    if (down.count(deskhub::MouseButton::Right)) return kCGEventRightMouseDragged;
    if (!down.empty()) return kCGEventOtherMouseDragged;
    return kCGEventMouseMoved;
}

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

CGPoint CursorPoint() {
    CGEventRef probe = CGEventCreate(nullptr);
    const CGPoint p = probe ? CGEventGetLocation(probe) : CGPointZero;
    if (probe) CFRelease(probe);
    return p;
}

CGRect DesktopBounds() {
    CGRect all = CGRectNull;
    for (NSScreen* s in [NSScreen screens])
        all = CGRectIsNull(all) ? s.frame : CGRectUnion(all, s.frame);
    return CGRectIsNull(all) ? CGRectMake(0, 0, 1920, 1080) : all;
}

}

InputInjector::InputInjector() {
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
    rectUs_ = 0;

    if (!macperm::HasAccessibility()) {
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
    if (r.size.width <= 0) return false;
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

void InputInjector::Apply(const deskhub::InputEvent& e) {
    if (!enabled_) return;

    if (localMon_ && localMon_->LocalActive(NowUs())) {
        if (!localSuppressed_) {
            localSuppressed_ = true;
            LOGI("[Input] Someone is using this Mac — remote input paused.");
        }
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
    if (!mackeys::WinVkToMac(vk, keycode)) return;

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
    CGMouseButton which = kCGMouseButtonLeft;
    if (buttonsDown_.count(deskhub::MouseButton::Right))
        which = kCGMouseButtonRight;
    else if (buttonsDown_.count(deskhub::MouseButton::Middle))
        which = kCGMouseButtonCenter;

    CGEventRef ev = CGEventCreateMouseEvent((CGEventSourceRef)source_, type,
        CGPointMake(x, y), which);
    if (!ev) return;
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
    const double x = rx + double(nx) / 65535.0 * rw;
    const double y = ry + double(ny) / 65535.0 * rh;
    PostMouseAt(x, y, 0, 0);
}

void InputInjector::SendMoveRelative(int32_t dx, int32_t dy) {
    const CGPoint cur = CursorPoint();
    const CGRect bounds = DesktopBounds();
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
