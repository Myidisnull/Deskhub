// =============================================================================
// InputInjector.cpp — cài đặt việc bơm input vào máy host bằng SendInput.
//
// BỐ CỤC
//   ScreenToVirtualDesk() — pixel màn hình → toạ độ chuẩn hoá của SendInput.
//   Init()                — chọn màn hình làm gốc toạ độ.
//   Apply()               — đường chính, phân nhánh theo loại event.
//   SendKey/SendButton/SendMove* — các lời gọi SendInput cụ thể.
//   ReleaseAll()          — nhả sạch phím/nút đang giữ.
//
// HAI TẦNG QUY ĐỔI TOẠ ĐỘ — dễ nhầm nếu không tách bạch
//   1. Client gửi 0..65535 tương đối với KHUNG HÌNH nó nhìn thấy = rect của màn
//      hình đang chia sẻ. → quy về pixel màn hình trong đúng rect đó.
//   2. SendInput lại đòi 0..65535 tương đối với MÀN HÌNH ẢO (mọi màn hình ghép lại).
//      → ScreenToVirtualDesk làm bước này.
//   Hai thang cùng dải 0..65535 nhưng gốc và độ dài khác hẳn nhau; nhầm chúng cho
//   ra con trỏ lệch chỗ mà vẫn "trông có vẻ đúng" ở màn hình đơn.
//
// TRẠNG THÁI PHẢI GIỮ: keysDown_ VÀ buttonsDown_
//   Đây không phải tối ưu mà là yêu cầu đúng đắn: không nhớ thì không nhả được khi
//   mất kết nối, và phím kẹt là lỗi tệ nhất của cả hệ thống (xem InputInjector.h).
//   keysDown_ khoá theo SCANCODE kèm bit E0, không phải theo vk — hai phím khác
//   nhau có thể cùng vk (Ctrl trái/phải) nhưng scancode luôn phân biệt được.
//
// LIÊN QUAN: input/InputInjector.h (hai cơ chế an toàn + ánh xạ toạ độ),
//            input/InputCapture.cpp (đầu kia), docs/07-input.md
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "input/InputInjector.h"

#include <cstdio>

#include "input/LocalInputMonitor.h"
#include "deskhubp/Clock.h"

#pragma comment(lib, "user32.lib")

namespace {

// "Host thắng": sau lần chuột/phím VẬT LÝ gần nhất của người ngồi máy, input từ
// xa bị bỏ qua thêm quãng này nữa. Đủ dài để host thao tác liền mạch không bị
// remote chen vào, đủ ngắn để remote lấy lại quyền gần như ngay khi host buông
// tay (cỡ ~1s là mức phổ biến cho heuristic này ở các tool điều khiển từ xa).
constexpr uint64_t kHostWinsGraceUs = 1'000'000;

// Đổi pixel màn hình -> tọa độ chuẩn hóa 0..65535 trên MÀN HÌNH ẢO (toàn bộ
// các màn hình ghép lại). MOUSEEVENTF_VIRTUALDESK bắt buộc khi máy nhiều màn
// hình, không thì chuột bị kẹt ở màn hình chính.
void ScreenToVirtualDesk(int px, int py, LONG& nx, LONG& ny) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    nx = vw > 1 ? LONG(int64_t(px - vx) * 65535 / (vw - 1)) : 0;
    ny = vh > 1 ? LONG(int64_t(py - vy) * 65535 / (vh - 1)) : 0;
}

DWORD ButtonFlag(deskhub::MouseButton b, bool down) {
    switch (b) {
        case deskhub::MouseButton::Left: return down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        case deskhub::MouseButton::Right: return down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        case deskhub::MouseButton::Middle: return down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        case deskhub::MouseButton::X1:
        case deskhub::MouseButton::X2: return down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
    }
    return 0;
}

} // namespace

bool InputInjector::Init(HMONITOR monitor) {
    if (!monitor) return false;
    monitor_ = monitor;
    return true;
}

void InputInjector::SetEnabled(bool on) {
    if (enabled_ == on) return;
    if (!on) ReleaseAll(); // tắt giữa chừng không được để kẹt phím
    enabled_ = on;
}

// Ưu tiên scancode, lùi về mã phím ảo chỉ khi client không gửi được scancode.
// Thứ tự ưu tiên này là điểm mấu chốt của cả tính năng điều khiển game — xem
// InputInjector.h. KEYEVENTF_EXTENDEDKEY cho các phím có tiền tố E0 (mũi tên,
// Ctrl/Alt phải, phím trên cụm numpad): thiếu cờ này thì mũi tên hoá thành phím số.
void InputInjector::SendKey(int32_t vk, int32_t scan, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    // Client chỉ có VK (bàn phím ảo mobile/web, xem KeyMap.h) -> tự tra scancode
    // theo layout của host. Bắt buộc cho game: engine đọc Raw Input/DirectInput chỉ
    // thấy scancode, wVk suông với chúng là vô hình.
    if (!(scan & 0xFF)) scan = int32_t(MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC));
    if (scan & 0xFF) {
        in.ki.wScan = WORD(scan & 0xFF);
        in.ki.dwFlags |= KEYEVENTF_SCANCODE;
        if (scan & deskhub::kScanExtended) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    } else {
        in.ki.wVk = WORD(vk); // client không gửi được scancode -> lùi về mã phím ảo
    }
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::SendButton(deskhub::MouseButton btn, bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = ButtonFlag(btn, down);
    if (btn == deskhub::MouseButton::X1) in.mi.mouseData = XBUTTON1;
    if (btn == deskhub::MouseButton::X2) in.mi.mouseData = XBUTTON2;
    if (in.mi.dwFlags) SendInput(1, &in, sizeof(INPUT));
}

// Quy đổi hai tầng — xem sơ đồ ở đầu file. Gốc là rect của monitor trên desktop ảo.
//
// Dùng (w-1) và 65535 làm mẫu số/tử số để hai đầu mút khớp chính xác: giá trị 65535
// phải rơi đúng vào pixel cuối cùng, không hụt một pixel. InputCapture chuẩn hoá
// theo đúng công thức nghịch đảo.
void InputInjector::SendMoveAbsolute(int32_t nx, int32_t ny) {
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor_, &mi)) return;
    const int w = mi.rcMonitor.right - mi.rcMonitor.left;
    const int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (w <= 1 || h <= 1) return;
    POINT pt{};
    pt.x = mi.rcMonitor.left + LONG(int64_t(nx) * (w - 1) / 65535);
    pt.y = mi.rcMonitor.top + LONG(int64_t(ny) * (h - 1) / 65535);

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    ScreenToVirtualDesk(pt.x, pt.y, in.mi.dx, in.mi.dy);
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::SendMoveRelative(int32_t dx, int32_t dy) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE; // không ABSOLUTE = delta, dùng cho game FPS
    in.mi.dx = dx;
    in.mi.dy = dy;
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::Apply(const deskhub::InputEvent& e) {
    if (!enabled_ || !monitor_) return;

    // Chốt "HOST THẮNG" (xem InputInjector.h): người ngồi tại máy vừa động
    // chuột/phím THẬT thì input từ xa nhường trong ~1s — hai bên cùng thao tác
    // thì người tại máy được ưu tiên, hết cảnh giằng con trỏ và lây phím bổ trợ
    // chéo. LocalInputMonitor đã lọc input tự bơm (cờ injected) nên không có
    // vòng tự-khoá; monitor không chạy thì mốc = 0 và chốt này tự tắt. Vào
    // trạng thái nhường là ReleaseAll ngay — remote đang giữ phím mà bị nhường
    // thì phím phải được nhả, không để kẹt.
    const uint64_t lastLocal = LocalInputMonitor::LastPhysicalUs();
    if (lastLocal && NowUs() - lastLocal < kHostWinsGraceUs) {
        if (!localSuppressed_) {
            localSuppressed_ = true;
            std::printf(
                "[Inject] Local user is active - pausing remote input (host wins).\n");
            ReleaseAll();
        }
        ++skipped_;
        return;
    }
    if (localSuppressed_) {
        localSuppressed_ = false;
        std::printf("[Inject] Local user idle - remote input resumed.\n");
    }
    ++applied_;

    switch (e.type) {
        case deskhub::InputType::Key: {
            const bool down = e.state != 0;
            // Nhớ theo scancode để ReleaseAll nhả đúng phím đã bơm.
            if (down)
                keysDown_[e.b] = e.a;
            else
                keysDown_.erase(e.b);
            SendKey(e.a, e.b, down);
            break;
        }
        case deskhub::InputType::MouseMove:
            if (e.absolute)
                SendMoveAbsolute(e.a, e.b);
            else
                SendMoveRelative(e.a, e.b);
            break;
        case deskhub::InputType::MouseButton: {
            const auto btn = deskhub::MouseButton(e.a);
            const bool down = e.state != 0;
            if (down)
                buttonsDown_.insert(btn);
            else
                buttonsDown_.erase(btn);
            SendButton(btn, down);
            break;
        }
        case deskhub::InputType::MouseWheel: {
            INPUT in{};
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_WHEEL;
            in.mi.mouseData = DWORD(e.b);
            SendInput(1, &in, sizeof(INPUT));
            break;
        }
    }
}

// Chốt an toàn: nhả sạch mọi thứ đang giữ. Gọi khi mất kết nối (BYE/timeout),
// khi client rời nguồn (SET_FOCUS false), và khi kết thúc phiên.
void InputInjector::ReleaseAll() {
    if (keysDown_.empty() && buttonsDown_.empty()) return;
    std::printf("[Inject] Releasing %zu keys + %zu mouse buttons still held.\n",
        keysDown_.size(), buttonsDown_.size());
    for (const auto& [scan, vk] : keysDown_) SendKey(vk, scan, false);
    for (auto btn : buttonsDown_) SendButton(btn, false);
    keysDown_.clear();
    buttonsDown_.clear();
}
