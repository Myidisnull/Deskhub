#pragma once
// =============================================================================
// PortalScreenCast.h — xin quyền quay màn hình qua xdg-desktop-portal (D-Bus).
//                      KHÔNG có đối ứng ở Windows/macOS: đây là bước Wayland bắt
//                      buộc phải có mà hai hệ kia không cần.
//
// VÌ SAO PHẢI CÓ CẢ MỘT FILE CHO VIỆC "MỞ CAPTURE"
//   Trên Windows, WGC bắt màn hình bằng một lời gọi API. Trên macOS,
//   ScreenCaptureKit cần quyền Screen Recording cấp MỘT LẦN trong System Settings
//   rồi thôi. Wayland thì KHÔNG cho ứng dụng nào đọc pixel của màn hình, chấm hết —
//   đó là điểm cốt lõi của mô hình bảo mật Wayland, không phải một thiếu sót.
//   Đường duy nhất là nhờ compositor làm hộ, qua xdg-desktop-portal:
//
//     app  ──D-Bus──► xdg-desktop-portal ──► compositor (GNOME/KDE/wlroots)
//                            │
//                            └── hiện HỘP THOẠI hệ thống cho người dùng chọn màn hình
//
//   Người dùng bấm đồng ý thì portal trả về một danh sách "stream" của PipeWire.
//   Không có đường nào đi vòng qua bước này.
//
// HỆ QUẢ VỀ MẶT UX — KHÁC HẲN WINDOWS/macOS, PHẢI NẮM
//   1. HỘP THOẠI HIỆN MỖI LẦN BẤM SHARE. Không phải cấp quyền một lần là xong.
//      (Portal có `persist_mode` + `restore_token` để nhớ lựa chọn giữa các lần
//      chạy — ta CÓ dùng, xem `restoreToken` bên dưới — nhưng ngay cả khi có token,
//      một số portal vẫn hiện hộp thoại xác nhận rút gọn.)
//   2. NGƯỜI DÙNG CHỌN MÀN HÌNH TRONG HỘP THOẠI CỦA HỆ THỐNG, không phải trong UI
//      của Deskhub. Nên luồng chọn nguồn ở đây NGƯỢC với macOS: bên macOS
//      SourceEnum liệt kê trước rồi người dùng tick trong app; ở đây ta gọi portal
//      trước, và danh sách trả về CHÍNH LÀ thứ người dùng đã chọn. Vì thế
//      GetShareSources() (capture/SourceEnum.h) chỉ là lớp mỏng gọi vào đây.
//   3. MỘT PHIÊN PORTAL CHO TẤT CẢ MÀN HÌNH. Portal trả N stream trong MỘT session
//      và MỘT fd PipeWire dùng chung. Nên lớp này là SINGLETON, không phải một đối
//      tượng cho mỗi nguồn như ScreenCapture bên macOS.
//
// TRÌNH TỰ D-BUS (org.freedesktop.portal.ScreenCast)
//   CreateSession        → session_handle
//   SelectSources        → chọn loại nguồn (MONITOR), nhiều nguồn, con trỏ nhúng
//   Start                → HỘP THOẠI hiện ở đây; trả về danh sách stream + node_id
//   OpenPipeWireRemote   → fd để pw_context_connect_fd()
//
//   Cả ba lời gọi đầu đều bất đồng bộ theo kiểu riêng của portal: hàm trả về ngay
//   một object path "Request", còn kết quả thật tới sau qua tín hiệu
//   org.freedesktop.portal.Request::Response. ⚠ Phải ĐĂNG KÝ NHẬN TÍN HIỆU TRƯỚC
//   KHI GỌI: portal có thể phát Response trước khi lời gọi phương thức kịp trả về,
//   và tín hiệu đó không được phát lại. Ta tự dựng object path của Request từ
//   handle_token của chính mình để đăng ký trước — cách làm chuẩn được khuyến nghị
//   trong tài liệu portal.
//
// ⚠ CHẶN, VÀ CÓ THỂ CHẶN RẤT LÂU
//   Open() chạy hộp thoại hệ thống và chờ người dùng bấm → phải gọi NGOÀI main
//   thread của GTK, nếu không cả UI đứng hình trong lúc hộp thoại mở. Nó dựng một
//   GMainContext riêng làm thread-default nên không giành giật gì với main loop
//   của GTK.
//
// LIÊN QUAN: capture/ScreenCapture.h (bên tiêu thụ nodeId + fd),
//            capture/SourceEnum.h (lớp mỏng cho UI), docs/17-linux-app.md §2
// =============================================================================
#include <cstdint>
#include <string>
#include <vector>

// Một màn hình portal đã cấp quyền cho ta quay.
struct PortalStream {
    uint32_t nodeId = 0; // node PipeWire — ScreenCapture kết nối vào đây
    // Vị trí + kích thước trong hệ toạ độ desktop toàn cục (portal prop "position"
    // và "size"). InputInjector cần chúng để quy đổi toạ độ chuột tuyệt đối; xem
    // input/InputInjector.h. Portal KHÔNG bắt buộc phải gửi "position" — thiếu thì
    // cả hai bằng 0 và ta coi màn hình này bắt đầu từ gốc.
    int32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
    std::string name; // nhãn hiện lên UI ("Screen 1 (2560×1440)")
};

class PortalScreenCast {
public:
    // Singleton: một phiên portal, một fd PipeWire, dùng chung cho mọi nguồn.
    static PortalScreenCast& Instance();

    // Chạy trọn trình tự D-Bus ở trên. CHẶN cho tới khi người dùng bấm xong hộp
    // thoại (hoặc hết hạn kQuestionTimeoutSec) → gọi ngoài main thread.
    //
    // Idempotent: đã mở rồi thì trả true ngay, KHÔNG hiện lại hộp thoại. Nhờ vậy
    // GetShareSources() (cho UI) và AgentLoop::Start() (cho phiên) gọi liên tiếp
    // nhau mà người dùng chỉ thấy đúng một hộp thoại.
    //
    // false = người dùng bấm huỷ, không có portal trên máy, hoặc D-Bus lỗi.
    bool Open();

    // Đóng session portal + trả fd. Sau lời gọi này, lần Open() kế tiếp sẽ hiện lại
    // hộp thoại. Gọi được nhiều lần.
    void Close();

    bool isOpen() const {
        return pipewireFd_ >= 0;
    }

    // fd để pw_context_connect_fd(). Lớp này SỞ HỮU fd; người dùng phải tự dup()
    // nếu cần giữ lâu hơn — pw_context_connect_fd() sẽ ĐÓNG fd nó nhận, nên
    // ScreenCapture bắt buộc phải truyền một bản dup (xem ScreenCapture.cpp).
    // -1 khi chưa mở.
    int pipewireFd() const {
        return pipewireFd_;
    }

    const std::vector<PortalStream>& streams() const {
        return streams_;
    }

    // Lý do thất bại gần nhất, để UI nói được điều gì đó tử tế thay vì im lặng.
    const std::string& lastError() const {
        return lastError_;
    }

private:
    PortalScreenCast() = default;
    ~PortalScreenCast();
    PortalScreenCast(const PortalScreenCast&) = delete;
    PortalScreenCast& operator=(const PortalScreenCast&) = delete;

    int pipewireFd_ = -1;
    std::string sessionHandle_; // object path của session portal, rỗng = chưa có
    // Token portal trả về để lần sau khỏi hỏi lại người dùng chọn màn hình nào.
    // Chỉ sống trong một lần chạy app (không ghi ra đĩa) — đủ để bấm Share/Stop
    // vài lần liên tiếp mà không phải chọn lại.
    std::string restoreToken_;
    std::vector<PortalStream> streams_;
    std::string lastError_;
};
