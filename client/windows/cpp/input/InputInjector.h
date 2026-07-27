#pragma once
// =============================================================================
// InputInjector.h — bơm input nhận được vào máy này, phía HOST (GĐ4).
//
// NHIỆM VỤ
//   Đối tác của InputCapture: nhận deskhub::InputEvent đã khử trùng và biến nó thành
//   thao tác thật trên máy host bằng SendInput.
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   InputCapture (client) → UDP ~~~> InputReceiver → **InputInjector** → SendInput
//
// ÁNH XẠ TOẠ ĐỘ
//   Client gửi toạ độ CHUẨN HOÁ (0..65535) trong khung hình nó nhìn thấy — đúng
//   vùng WGC capture = rect của MÀN HÌNH đang chia sẻ. Host quy đổi ngược về pixel
//   trong rect đó, rồi đổi tiếp sang toạ độ màn hình ảo cho SendInput. Nhờ đi qua
//   thang chuẩn hoá này mà client thu nhỏ cửa sổ preview bao nhiêu tuỳ ý vẫn trỏ
//   đúng chỗ.
//
// BƠM BẰNG SCANCODE, KHÔNG PHẢI MÃ PHÍM ẢO
//   KEYEVENTF_SCANCODE. Game dùng DirectInput/Raw Input đọc thẳng scancode; gửi vk
//   không thôi thì game không thấy gì. Đây là lý do phần lớn công cụ điều khiển từ
//   xa không chơi được game — và là điểm đối xứng với InputCapture ở đầu kia.
//
// ⚠ HAI CƠ CHẾ AN TOÀN
//   1. CHỐNG KẸT PHÍM (ReleaseAll). Injector nhớ mọi phím/nút đang giữ. Client mất
//      kết nối giữa lúc đang giữ W thì sự kiện nhả không bao giờ tới, và nhân vật
//      chạy mãi. AgentLoop gọi ReleaseAll khi BYE/timeout/SET_FOCUS(false).
//   2. HOST THẮNG (LocalInputMonitor). Hai bên cùng thao tác thì input trộn thẳng
//      vào nhau: giằng con trỏ, phím bổ trợ lây chéo (host giữ Ctrl thật + remote
//      gõ S = Ctrl+S). Người ngồi tại máy vừa động chuột/phím THẬT là input từ xa
//      nhường trong ~1s — xem input/LocalInputMonitor.h và Apply().
//   (Chốt foreground cũ — chỉ bơm khi CỬA SỔ chia sẻ đang foreground — đã bỏ cùng
//   share-theo-cửa-sổ 2026-07-27. Nguồn là cả màn hình thì mọi thứ trên đó đều
//   thuộc phạm vi chia sẻ, input đi vào app đang foreground là ĐÚNG hành vi.)
//
// MÔ HÌNH LUỒNG
//   Apply() được gọi từ luồng Recv của AgentLoop.
//
// LIÊN QUAN: input/InputCapture.h (đầu kia), deskhub/input/InputReceiver.h,
//            client/windows/cpp/AgentLoop.cpp (nơi gọi ReleaseAll), docs/07-input.md
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <map>
#include <set>

#include "deskhub/protocol/Wire.h"

class InputInjector {
public:
    // `monitor` = màn hình đang chia sẻ (gốc tọa độ cho chuột tuyệt đối).
    // Trả false nếu handle rỗng.
    bool Init(HMONITOR monitor);

    // Bơm một event. Bỏ qua nếu đang tắt hoặc đang nhường "host thắng".
    void Apply(const deskhub::InputEvent& e);

    // Nhả mọi phím/nút còn đang giữ (mất kết nối, kết thúc phiên, tắt input,
    // client rời nguồn — SET_FOCUS(false)).
    void ReleaseAll();

    void SetEnabled(bool on);
    bool enabled() const {
        return enabled_;
    }

    uint64_t applied() const {
        return applied_;
    }
    uint64_t skipped() const {
        return skipped_;
    }

private:
    void SendKey(int32_t vk, int32_t scan, bool down);
    void SendButton(deskhub::MouseButton btn, bool down);
    void SendMoveAbsolute(int32_t nx, int32_t ny);
    void SendMoveRelative(int32_t dx, int32_t dy);

    HMONITOR monitor_ = nullptr;
    bool enabled_ = true;
    bool localSuppressed_ = false; // đang nhường "host thắng" — log một lần mỗi lượt
    uint64_t applied_ = 0;
    uint64_t skipped_ = 0;                // event bị bỏ vì đang nhường người ngồi tại máy
    std::map<int32_t, int32_t> keysDown_; // scancode (kèm bit E0) -> mã phím ảo
    std::set<deskhub::MouseButton> buttonsDown_;
};
