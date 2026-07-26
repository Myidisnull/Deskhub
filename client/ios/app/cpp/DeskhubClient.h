// =============================================================================
// DeskhubClient.h — mặt tiền C duy nhất cho Swift gọi xuống tầng C++.
//                   Đối ứng JniBridge.cpp + NativeClient.kt bên Android.
//
// VÌ SAO LÀ HÀM C THẦ CHỨ KHÔNG PHẢI LỚP OBJ-C
//   Bridging header của Swift nhập hàm C nguyên bản. Lớp Obj-C vẫn dùng được nhưng
//   thêm một lớp dispatch không cần thiết khi facade chỉ là bọc mỏng cho một đối
//   tượng toàn cục (g_client). Hàm C giữ API phẳng, dễ đọc, dễ test.
//
// MÔ HÌNH "MỘT PHIÊN TOÀN CỤC"
//   App chỉ xem MỘT nguồn tại một thời điểm (view-only v1) nên toàn bộ trạng thái
//   nằm trong biến static duy nhất bên .mm. Đây là đối ứng chính xác của biến toàn
//   cục g_client trong JniBridge.cpp bên Android.
//
// THREAD SAFETY
//   Mọi hàm an toàn gọi từ main thread. listSources CHẶN ~3s nên PHẢI gọi ngoài
//   main (Swift dùng Task.detached). start/stop/setLayer từ main thread. phase/
//   statusLine/videoWidth/videoHeight thread-safe nhờ atomic/mutex bên trong.
//
// LIÊN QUAN: DeskhubClient.mm (cài đặt), app/DeskhubClient.swift (bọc Swift),
//            client/android/.../JniBridge.cpp (bản song song)
// =============================================================================
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Trạng thái phiên mà UI quan tâm.
typedef enum {
    DHPhaseIdle = 0,
    DHPhaseConnecting = 1,
    DHPhaseStreaming = 2,
    DHPhaseEnded = 3,
    // GĐ10: host đòi mật khẩu mà máy này chưa có. Phiên VẪN SỐNG — tầng C++ tiếp tục
    // phát lại HELLO, nên chỉ cần dh_submit_password là nó đi tiếp. ĐỪNG gọi dh_stop.
    DHPhaseNeedPassword = 4,
} DHPhase;

// Vì sao host từ chối. Trùng deskhub::RejectReason bên C++ (Wire.h) — cùng kiểu enum
// bị tách làm đôi qua ranh giới ngôn ngữ, nên sửa một bên phải sửa cả bên kia.
typedef enum {
    DHRejectNone = 0,
    DHRejectBusy = 1,
    DHRejectCodecMismatch = 2,
    DHRejectAuthRequired = 3,
    DHRejectAuthFailed = 4,
    DHRejectLockedOut = 5,
} DHRejectReason;

// Thông tin một nguồn (cửa sổ) host đang chia sẻ.
typedef struct {
    uint8_t sourceId;
    uint16_t width;
    uint16_t height;
    char name[256];
} DHSourceInfo;

// Hỏi host đang chia sẻ gì. CHẶN ~3s, gọi ngoài main thread.
// Trả về số nguồn tìm thấy (0 = host im lặng / bản cũ).
int dh_list_sources(const char* address, DHSourceInfo* out, int capacity);

// Bắt đầu phiên xem. Trả true nếu khởi động thành công.
//
// Bốn tham số sau là của GĐ10 (xác thực), do Credentials.swift nạp từ Keychain:
//   client_id    — danh tính ỔN ĐỊNH của bản cài này. Host khoá danh sách thiết bị
//                  tin cậy theo nó; đổi mỗi lần chạy là token không bao giờ khớp lại.
//   device_name  — tên hiện ở danh sách "Trusted devices" phía host.
//   password     — mật khẩu đã lưu cho host này (NULL/rỗng = chưa có → host đòi thì
//                  phiên vào DHPhaseNeedPassword).
//   device_token — token host cấp lần trước (NULL/0 = chưa từng được nhớ).
bool dh_start(const char* address, uint8_t sourceId, uint32_t client_id,
    const char* device_name, const char* password,
    const uint8_t* device_token, int device_token_len);

// --- Xác thực (GĐ10) ---

// Người dùng vừa nhập mật khẩu. Có tác dụng ở lần phát lại HELLO kế tiếp (≤ 0.5s);
// KHÔNG phải kết nối lại từ đầu.
void dh_submit_password(const char* password);

// Token host vừa cấp để nhớ máy này. ĐỌC RỒI XOÁ — gọi lần hai trả 0. Chép ≤ `cap`
// byte vào `out`, trả số byte thật (0 = chưa có gì mới). Gọi mỗi nhịp poll và cất
// ngay: token chỉ đi trên dây ĐÚNG MỘT LẦN.
int dh_take_device_token(uint8_t* out, int cap);

// Vì sao host từ chối lần bắt tay gần nhất (DHRejectReason).
int dh_reject_reason(void);

// Dừng phiên hiện tại.
void dh_stop(void);

// Giao/thu hồi layer. `layer` là AVSampleBufferDisplayLayer* (__bridge void*),
// hoặc NULL khi app xuống nền / view biến mất. CHẶN main thread cho tới khi
// thread Decode xác nhận đã buông layer cũ.
void dh_set_layer(void* layer);

// Gõ một phím rời (nhấn + nhả) sang host — nút F9 trên header màn hình xem.
// `vk` là mã phím ảo Windows, `scan` là scancode (bit8 = cờ E0, xem Wire.h).
// Chỉ có tác dụng khi phiên đang STREAMING.
void dh_key_tap(int32_t vk, int32_t scan);

// Tổ hợp kiểu Ctrl+C: giữ phím bổ trợ, gõ phím chính, nhả theo đúng thứ tự.
void dh_key_chord(int32_t mod_vk, int32_t mod_scan, int32_t vk, int32_t scan);

// Chuột tuyệt đối từ touch: toạ độ chuẩn hoá 0..65535 trong khung video.
void dh_mouse_move(int32_t nx, int32_t ny);

// Chuột tương đối — chế độ khoá chuột cho game FPS (nút Lock): delta thô.
void dh_mouse_move_rel(int32_t dx, int32_t dy);

// Nhấn/nhả nút chuột (1 = trái, 2 = phải) tại vị trí con trỏ hiện hành.
void dh_mouse_button(int32_t button, bool down);

// Gõ một ký tự từ bàn phím ảo (KeyMap của core quy đổi sang VK, layout US).
void dh_char_tap(uint32_t codepoint);

// Trạng thái phiên hiện tại.
DHPhase dh_phase(void);

// Dòng số liệu cho overlay (fps/kbps/RTT/e2e). Trả chuỗi tĩnh, hợp lệ tới
// lần gọi kế (không cần free). Rỗng khi chưa có số liệu.
const char* dh_status_line(void);

// Lý do phiên kết thúc. Rỗng nếu chưa kết thúc.
const char* dh_end_reason(void);

// Kích thước video đàm phán được. 0 nếu chưa đàm phán xong.
uint32_t dh_video_width(void);
uint32_t dh_video_height(void);

#ifdef __cplusplus
}
#endif
