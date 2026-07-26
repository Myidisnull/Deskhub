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
//   vùng ScreenCapture đang bắt: toàn bộ cửa sổ, hoặc toàn bộ màn hình. Host quy đổi
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
// ⚠ BA CƠ CHẾ AN TOÀN / ƯU TIÊN (giữ nguyên tinh thần bản Windows)
//   1. CHỐT FOREGROUND (TargetHasFocus). CGEventPost bơm vào ứng dụng ĐANG
//      FOREGROUND chứ không vào một cửa sổ cụ thể. Người ngồi ở máy host bấm sang
//      ứng dụng khác thì mọi phím/chuột từ xa sẽ rơi vào ứng dụng đó — vừa sai vừa
//      NGUY HIỂM (người điều khiển từ xa vô tình gõ vào Mail, Terminal của chủ máy).
//      Nên: chỉ bơm khi ứng dụng sở hữu cửa sổ đang chia sẻ thật sự đang foreground.
//      Nguồn là CẢ MÀN HÌNH thì không có chốt này — không tồn tại "ứng dụng khác"
//      nằm ngoài thứ đã chia sẻ.
//   2. CHỐNG KẸT PHÍM (ReleaseAll). Injector nhớ mọi phím/nút đang giữ. Client mất
//      kết nối giữa lúc đang giữ W thì sự kiện nhả không bao giờ tới, và nhân vật
//      chạy mãi. AgentLoop gọi ReleaseAll khi BYE/timeout.
//   3. HOST THẮNG (LocalInputMonitor). Người ngồi tại máy vừa động chuột/phím THẬT
//      thì input từ xa nhường trong ~1 giây — xem agent/LocalInputMonitor.h.
//
// ⚠ KHÔNG CÓ QUYỀN ACCESSIBILITY = IM LẶNG HỎNG
//   CGEventPost KHÔNG trả mã lỗi khi thiếu quyền: nó "thành công" và không sự kiện
//   nào tới đích. Đây là bản macOS của bẫy UIPI bên Windows. Init() vì thế kiểm tra
//   macperm::HasAccessibility() và ghi log thẳng thắn — xem agent/Permissions.h.
//
// MÔ HÌNH LUỒNG
//   Apply()/ReleaseAll() gọi từ thread Recv của AgentLoop. Init/FocusTarget cũng vậy.
//
// LIÊN QUAN: agent/LocalInputMonitor.h, agent/Permissions.h, input/MacKeyMap.h,
//            deskhub/input/InputReceiver.h, docs/07-input.md,
//            client/windows/input/InputInjector.h (bản song song)
// =============================================================================
#include <cstdint>
#include <map>
#include <set>

#include "agent/CaptureTypes.h"
#include "deskhub/wire/Wire.h"

class LocalInputMonitor;

class InputInjector {
public:
    InputInjector();
    ~InputInjector();
    InputInjector(const InputInjector&) = delete;
    InputInjector& operator=(const InputInjector&) = delete;

    // `target` = nguồn đang chia sẻ (gốc toạ độ + đích của chốt foreground).
    // Trả false nếu nguồn không hợp lệ hoặc thiếu quyền Accessibility.
    bool Init(const CaptureTarget& target);

    // Kéo ứng dụng sở hữu nguồn lên foreground (client vừa chuyển sang xem nguồn
    // này — SET_FOCUS). Không làm gì và trả true nếu nguồn là cả màn hình, hoặc nếu
    // ứng dụng vốn đã foreground. Trả false khi macOS từ chối.
    bool FocusTarget();

    // Bơm một event. Bỏ qua nếu đang tắt, nguồn đã biến mất, ứng dụng đích không
    // foreground, hoặc người ngồi tại máy vừa dùng chuột/phím thật.
    void Apply(const deskhub::InputEvent& e);

    // Nhả mọi phím/nút còn đang giữ (mất kết nối, kết thúc phiên, tắt input).
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
    // CGEventPost bơm vào ứng dụng ĐANG FOREGROUND — xem cơ chế 1 ở đầu file.
    bool TargetHasFocus();
    // Rect của nguồn theo ĐIỂM, hệ toạ độ toàn cục gốc trên-trái. false = nguồn đã
    // biến mất. Có cache vì chuột di chuyển sinh hàng trăm event mỗi giây, còn cửa
    // sổ thì hiếm khi bị kéo.
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

    CaptureTarget target_{};
    void* source_ = nullptr; // CGEventSourceRef
    LocalInputMonitor* localMon_ = nullptr;

    bool enabled_ = true;
    bool hadFocus_ = true;         // để chỉ log một lần mỗi khi đổi trạng thái focus
    bool localSuppressed_ = false; // đang nhường "host thắng" — log một lần mỗi lượt
    uint64_t applied_ = 0;
    uint64_t skipped_ = 0; // event bị bỏ vì đích không foreground / host đang dùng máy

    // Cache rect nguồn (điểm, toàn cục) + hạn dùng.
    double rectX_ = 0, rectY_ = 0, rectW_ = 0, rectH_ = 0;
    uint64_t rectUs_ = 0;
    int32_t ownerPid_ = 0; // pid ứng dụng sở hữu cửa sổ nguồn (0 = màn hình)

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
