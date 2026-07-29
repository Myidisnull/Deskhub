#pragma once
// =============================================================================
// InputInjector.h — bơm input nhận được vào máy này, phía HOST (GĐ4).
//                   Đối ứng client/windows/cpp/input/InputInjector.h (SendInput)
//                   và client/macos/.../input/InputInjector.h (CGEventPost).
//
// NHIỆM VỤ
//   Nhận deskhub::InputEvent đã khử trùng và biến nó thành thao tác thật trên máy
//   Ubuntu này bằng /dev/uinput.
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   client → UDP ~~~> InputReceiver → **InputInjector** → /dev/uinput → kernel
//                                                       → libinput → compositor
//
// ⚠ VÌ SAO uinput CHỨ KHÔNG PHẢI XTest HAY PORTAL RemoteDesktop
//   - XTest chỉ chạy trên Xorg. Phiên Wayland (mặc định của Ubuntu 22.04+) không
//     có nó, và XWayland thì chỉ bơm được vào ứng dụng X11, không vào ứng dụng
//     Wayland gốc — tức là bơm được vào một nửa màn hình.
//   - Portal RemoteDesktop bơm được đúng chỗ nhưng phải xin quyền qua MỘT hộp
//     thoại NỮA, và nó chỉ nhận toạ độ tương đối trên nhiều compositor.
//   - uinput dựng một BÀN PHÍM VÀ CHUỘT ẢO ở tầng kernel. Compositor không phân
//     biệt được nó với phần cứng thật, nên input tới được MỌI ứng dụng, cả Wayland
//     lẫn X11, cả màn hình khoá. Cái giá là quyền ghi /dev/uinput — xem §7 của
//     docs/17-linux-app.md (một quy tắc udev, cấp một lần).
//
// ⚠ BA THIẾT BỊ ẢO, KHÔNG PHẢI MỘT — VÀ ĐÓ LÀ BẮT BUỘC
//   libinput phân loại thiết bị theo tập sự kiện nó khai báo, và một thiết bị vừa
//   có trục TƯƠNG ĐỐI vừa có trục TUYỆT ĐỐI sẽ bị nó coi là chuột thường rồi BỎ
//   QUA phần tuyệt đối. Nên phải tách:
//     1. "Deskhub Keyboard"        — EV_KEY, toàn bộ phím trong LinuxKeyMap.
//     2. "Deskhub Mouse"           — EV_REL (X/Y/bánh xe) + ba nút. Mọi cú NHẤN
//        đều phát từ đây, kể cả khi con trỏ vừa được đặt bằng thiết bị (3).
//     3. "Deskhub Absolute Mouse"  — EV_ABS (X/Y, thang 0..65535) + BTN_LEFT khai
//        báo nhưng KHÔNG BAO GIỜ phát. BTN_LEFT phải có mặt, nếu không libinput
//        coi thiết bị này là bảng vẽ/joystick chứ không phải con trỏ.
//
// ÁNH XẠ TOẠ ĐỘ — HAI PHÉP ĐỔI LỒNG NHAU
//   Client gửi toạ độ CHUẨN HOÁ 0..65535 trong khung hình nó nhìn thấy, tức là
//   trong MÀN HÌNH ĐANG CHIA SẺ. Thiết bị tuyệt đối của uinput thì trải trên TOÀN
//   BỘ desktop (mọi màn hình ghép lại). Nên:
//        chuẩn hoá → toạ độ trong màn hình chia sẻ → toạ độ desktop toàn cục
//                  → chuẩn hoá lại theo bao hình desktop → giá trị ABS
//   Thiếu bước giữa thì trên máy hai màn hình, con trỏ luôn chạy sang màn hình
//   trái và chỉ dùng được nửa quãng đường. Vị trí/kích thước màn hình chia sẻ lấy
//   từ portal (capture/PortalScreenCast.h), bao hình desktop do tầng UI đo bằng
//   GDK rồi truyền vào (AgentOptions).
//
// ⚠ HAI CƠ CHẾ AN TOÀN / ƯU TIÊN (giữ nguyên tinh thần bản Windows/macOS)
//   1. CHỐNG KẸT PHÍM (ReleaseAll). Injector nhớ mọi phím/nút đang giữ. Client mất
//      kết nối giữa lúc đang giữ W thì sự kiện nhả không bao giờ tới, và nhân vật
//      chạy mãi. AgentLoop gọi ReleaseAll khi BYE/timeout/SET_FOCUS(false).
//   2. HOST THẮNG (LocalInputMonitor). Người ngồi tại máy vừa động chuột/phím THẬT
//      thì input từ xa nhường trong ~1 giây — xem input/LocalInputMonitor.h.
//
// MÔ HÌNH LUỒNG
//   Init/Apply/ReleaseAll gọi từ thread Recv của AgentLoop.
//
// LIÊN QUAN: input/LocalInputMonitor.h, input/LinuxKeyMap.h,
//            deskhub/input/InputReceiver.h, docs/07-input.md,
//            docs/17-linux-app.md §5
// =============================================================================
#include <cstdint>
#include <map>
#include <set>

#include "deskhub/protocol/Wire.h"

class LocalInputMonitor;

class InputInjector {
public:
    // Tên thiết bị ảo. LocalInputMonitor dùng CHÍNH những chuỗi này để bỏ qua
    // input do ta tự bơm ra — xem "lọc chính input mình bơm" ở LocalInputMonitor.h.
    static constexpr const char* kKeyboardName = "Deskhub Keyboard";
    static constexpr const char* kPointerName = "Deskhub Mouse";
    static constexpr const char* kAbsPointerName = "Deskhub Absolute Mouse";

    InputInjector();
    ~InputInjector();
    InputInjector(const InputInjector&) = delete;
    InputInjector& operator=(const InputInjector&) = delete;

    // `src*` = vị trí + kích thước MÀN HÌNH ĐANG CHIA SẺ trong desktop toàn cục.
    // `desk*` = bao hình của TOÀN BỘ desktop. desk rỗng (deskW/H = 0) thì coi như
    // desktop chỉ có đúng màn hình này.
    // false = không mở được /dev/uinput (thiếu quyền — xem docs/17 §7).
    bool Init(int32_t srcX, int32_t srcY, uint32_t srcW, uint32_t srcH, int32_t deskX,
        int32_t deskY, uint32_t deskW, uint32_t deskH);

    // Bơm một event. Bỏ qua nếu đang tắt, chưa Init, hoặc người ngồi tại máy vừa
    // dùng chuột/phím thật ("host thắng").
    void Apply(const deskhub::InputEvent& e);

    // Nhả mọi phím/nút còn đang giữ (mất kết nối, kết thúc phiên, client rời
    // nguồn — SET_FOCUS(false)).
    void ReleaseAll();

    void SetEnabled(bool on) {
        enabled_ = on;
    }
    bool enabled() const {
        return enabled_;
    }

    // Bộ theo dõi input cục bộ dùng chung cho mọi nguồn (AgentLoop sở hữu).
    // nullptr = không áp dụng "host thắng".
    void SetLocalMonitor(LocalInputMonitor* mon) {
        localMon_ = mon;
    }

    uint64_t applied() const {
        return applied_;
    }
    uint64_t skipped() const {
        return skipped_;
    }

private:
    void SendKey(int32_t vk, bool down);
    void SendButton(deskhub::MouseButton btn, bool down);
    void SendMoveAbsolute(int32_t nx, int32_t ny);
    void SendMoveRelative(int32_t dx, int32_t dy);
    void SendWheel(int32_t delta);

    int kbdFd_ = -1;
    int mouseFd_ = -1;
    int absFd_ = -1;

    // Hình học, đặt một lần ở Init.
    int32_t srcX_ = 0, srcY_ = 0;
    uint32_t srcW_ = 0, srcH_ = 0;
    int32_t deskX_ = 0, deskY_ = 0;
    uint32_t deskW_ = 0, deskH_ = 0;

    LocalInputMonitor* localMon_ = nullptr;
    bool enabled_ = true;
    bool localSuppressed_ = false; // đang nhường "host thắng" — log một lần mỗi lượt
    uint64_t applied_ = 0;
    uint64_t skipped_ = 0;

    // Sổ phím/nút đang giữ, để ReleaseAll biết phải nhả gì.
    std::map<int32_t, uint16_t> keysDown_; // VK -> mã evdev
    std::set<deskhub::MouseButton> buttonsDown_;
};
