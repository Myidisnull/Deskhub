#pragma once
// =============================================================================
// InputInjector.h — bơm input nhận được vào máy này, phía HOST (GĐ4).
//                   Đối ứng client/windows/input/InputInjector.h; cùng bài toán,
//                   khác API (CGEvent thay SendInput).
//
// NHIỆM VỤ
//   Nhận deskhub::InputEvent đã khử trùng và biến nó thành thao tác thật trên máy
//   Mac này bằng Quartz Event Services.
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   InputCapture (client) → UDP ~~~> InputReceiver → **InputInjector** → CGEventPost
//
// ÁNH XẠ TOẠ ĐỘ
//   Client gửi toạ độ CHUẨN HOÁ (0..65535) trong khung hình nó nhìn thấy — đúng
//   vùng ScreenCapture đang bắt: toàn bộ màn hình được chia sẻ. Host quy đổi
//   ngược về ĐIỂM (point) trong hệ toạ độ màn hình toàn cục của Quartz (gốc trên
//   trái). Nhờ đi qua thang chuẩn hoá này mà client thu nhỏ cửa sổ xem bao nhiêu tuỳ
//   ý vẫn trỏ đúng chỗ.
//
//   ⚠ ĐIỂM chứ không phải PIXEL. Khung capture tính bằng pixel (đã nhân backing
//   scale), còn CGEvent nhận điểm. Trên màn Retina hai số chênh nhau 2 lần — nhầm là
//   con trỏ chạy đúng nửa quãng đường. Ta chia ngược lại ở đây, một chỗ duy nhất.
//
// BƠM BẰNG KEYCODE macOS, DỊCH TỪ VK WINDOWS
//   Giao thức nói bằng VK Windows (Wire.h). MacKeyMap dịch sang virtual keycode của
//   Carbon. Ta ưu tiên VK chứ không phải scancode — ngược với bản Windows, và có lý
//   do: scancode là mã BÀN PHÍM PC, macOS không có khái niệm tương ứng, còn VK thì
//   ánh xạ 1-1 sang keycode Carbon cho mọi phím thường dùng.
//
// ⚠ HAI CƠ CHẾ AN TOÀN / ƯU TIÊN (giữ nguyên tinh thần bản Windows)
//   1. CHỐNG KẸT PHÍM (ReleaseAll). Injector nhớ mọi phím/nút đang giữ. Client mất
//      kết nối giữa lúc đang giữ W thì sự kiện nhả không bao giờ tới, và nhân vật
//      chạy mãi. AgentLoop gọi ReleaseAll khi BYE/timeout/SET_FOCUS(false).
//   2. HOST THẮNG (LocalInputMonitor). Người ngồi tại máy vừa động chuột/phím THẬT
//      thì input từ xa nhường trong ~1 giây — xem input/LocalInputMonitor.h.
//   (Chốt foreground cũ — chỉ bơm khi app sở hữu CỬA SỔ chia sẻ đang frontmost —
//   đã bỏ cùng share-theo-cửa-sổ 2026-07-27: nguồn là cả màn hình thì mọi thứ trên
//   đó đều thuộc phạm vi chia sẻ, input vào app đang foreground là ĐÚNG hành vi.)
//
// ⚠ KHÔNG CÓ QUYỀN ACCESSIBILITY = IM LẶNG HỎNG
//   CGEventPost KHÔNG trả mã lỗi khi thiếu quyền: nó "thành công" và không sự kiện
//   nào tới đích. Đây là bản macOS của bẫy UIPI bên Windows. Init() vì thế kiểm tra
//   macperm::HasAccessibility() và ghi log thẳng thắn — xem Permissions.h.
//
// MÔ HÌNH LUỒNG
//   Apply()/ReleaseAll() gọi từ thread Recv của AgentLoop. Init cũng vậy.
//
// LIÊN QUAN: input/LocalInputMonitor.h, Permissions.h, input/MacKeyMap.h,
//            deskhub/input/InputReceiver.h, docs/07-input.md,
//            client/windows/input/InputInjector.h (bản song song)
// =============================================================================
#include <cstdint>
#include <map>
#include <set>

#include "deskhub/protocol/Wire.h"

class LocalInputMonitor;

class InputInjector {
public:
    InputInjector();
    ~InputInjector();
    InputInjector(const InputInjector&) = delete;
    InputInjector& operator=(const InputInjector&) = delete;

    // `displayId` = màn hình đang chia sẻ (gốc toạ độ cho chuột tuyệt đối).
    // Trả false nếu id rỗng hoặc màn hình không có bounds.
    bool Init(uint32_t displayId);

    // Bơm một event. Bỏ qua nếu đang tắt, màn hình đã bị rút, hoặc người ngồi tại
    // máy vừa dùng chuột/phím thật ("host thắng").
    void Apply(const deskhub::InputEvent& e);

    // Nhả mọi phím/nút còn đang giữ (mất kết nối, kết thúc phiên, tắt input,
    // client rời nguồn — SET_FOCUS(false)).
    void ReleaseAll();

    void SetEnabled(bool on);
    bool enabled() const {
        return enabled_;
    }

    // Bộ theo dõi input cục bộ dùng chung cho mọi nguồn (AgentLoop sở hữu). nullptr
    // = không áp dụng "host thắng".
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
    // Rect của màn hình theo ĐIỂM, hệ toạ độ toàn cục gốc trên-trái. false = màn
    // hình đã bị rút. Có cache vì chuột di chuyển sinh hàng trăm event mỗi giây,
    // còn cấu hình màn hình thì hiếm khi đổi.
    bool SourceRect(double& x, double& y, double& w, double& h);
    void SendKey(int32_t vk, bool down);
    void SendButton(deskhub::MouseButton btn, bool down);
    void SendMoveAbsolute(int32_t nx, int32_t ny);
    void SendMoveRelative(int32_t dx, int32_t dy);
    void SendWheel(int32_t delta);
    // Đặt con trỏ tới `x`,`y` (điểm, toàn cục) kèm delta thô cho game đọc.
    void PostMouseAt(double x, double y, int32_t dx, int32_t dy);
    // Cờ modifier hiện hành, dựng từ modsDown_. Trả CGEventFlags dưới dạng uint64.
    uint64_t CurrentFlags() const;

    uint32_t displayId_ = 0; // CGDirectDisplayID của màn hình đang chia sẻ
    void* source_ = nullptr; // CGEventSourceRef
    LocalInputMonitor* localMon_ = nullptr;

    bool enabled_ = true;
    bool localSuppressed_ = false; // đang nhường "host thắng" — log một lần mỗi lượt
    uint64_t applied_ = 0;
    uint64_t skipped_ = 0; // event bị bỏ vì host đang dùng máy

    // Cache rect màn hình (điểm, toàn cục) + hạn dùng.
    double rectX_ = 0, rectY_ = 0, rectW_ = 0, rectH_ = 0;
    uint64_t rectUs_ = 0;

    // Sổ phím/nút đang giữ, để ReleaseAll biết phải nhả gì.
    std::map<int32_t, uint16_t> keysDown_; // VK -> keycode macOS
    std::set<deskhub::MouseButton> buttonsDown_;
    std::set<int32_t> modsDown_; // VK của các phím bổ trợ đang giữ

    // Đếm click liên tiếp cho double/triple-click. macOS KHÔNG tự suy ra: ứng dụng
    // đọc thẳng trường clickState của sự kiện, nên không đặt thì hai cú click nhanh
    // chỉ là hai click đơn và không mở được thư mục trong Finder.
    uint64_t lastClickUs_ = 0;
    double lastClickX_ = 0, lastClickY_ = 0;
    int64_t clickState_ = 1;
    deskhub::MouseButton lastClickBtn_ = deskhub::MouseButton::Left;
};
