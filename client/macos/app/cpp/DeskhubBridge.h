#pragma once
// =============================================================================
// DeskhubBridge.h — mặt tiền C duy nhất cho Swift gọi xuống tầng C++.
//                   Đối ứng client/ios/app/cpp/DeskhubClient.h, mở rộng cho VAI HOST.
//
// VÌ SAO LÀ HÀM C THUẦN CHỨ KHÔNG PHẢI LỚP OBJ-C
//   Bridging header của Swift nhập hàm C nguyên bản. Lớp Obj-C vẫn dùng được nhưng
//   thêm một lớp dispatch không cần thiết khi facade chỉ là bọc mỏng cho hai đối
//   tượng toàn cục. Hàm C giữ API phẳng, dễ đọc.
//
// MÔ HÌNH "MỘT PHIÊN MỖI VAI"
//   Đúng hai biến static bên .mm: g_client (đang xem máy khác) và g_agent (đang chia
//   sẻ máy này). Chúng ĐỘC LẬP — một chiếc Mac vừa chia sẻ màn hình vừa xem máy khác
//   là chuyện hợp lệ, đúng tinh thần "một app chứa cả hai vai" (docs/01 §1).
//
// TIỀN TỐ
//   dh_*  — vai CLIENT (xem máy khác) + tiện ích dùng chung (quyền, bảng phím).
//   dha_* — vai AGENT (chia sẻ máy này).
//
// ⚠ HÀM NÀO CHẶN
//   dh_list_sources        ~3 giây (hỏi host qua UDP)
//   dha_list_share_sources ~2 giây (hỏi ScreenCaptureKit)
//   dha_start              tới ~10 giây (đợi frame đầu của từng nguồn)
//   Ba hàm đó PHẢI gọi ngoài main thread (Swift: Task.detached). Mọi hàm còn lại an
//   toàn và nhanh trên main thread.
//
// LIÊN QUAN: DeskhubBridge.mm (cài đặt), swift/DeskhubClient.swift +
//            swift/DeskhubAgent.swift (bọc Swift), client/ClientLoop.h,
//            agent/AgentLoop.h
// =============================================================================

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===========================================================================
// Vai CLIENT — xem và điều khiển máy khác
// ===========================================================================

// Trạng thái phiên mà UI quan tâm.
typedef enum {
    DHPhaseIdle = 0,
    DHPhaseConnecting = 1,
    DHPhaseStreaming = 2,
    DHPhaseEnded = 3,
} DHPhase;

// Thông tin một nguồn (cửa sổ) mà host đang chia sẻ.
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
bool dh_start(const char* address, uint8_t sourceId);

// Dừng phiên hiện tại.
void dh_stop(void);

// Giao/thu hồi layer. `layer` là AVSampleBufferDisplayLayer* (__bridge void*), hoặc
// NULL khi cửa sổ đóng / view biến mất. CHẶN main thread cho tới khi thread Decode
// xác nhận đã buông layer cũ.
void dh_set_layer(void* layer);

// --- Kênh input. Chỉ có tác dụng khi phiên đang STREAMING. ---

// Nhấn/nhả một phím vật lý. `vk`/`scan` do dh_map_key dịch từ NSEvent.keyCode.
void dh_key(int32_t vk, int32_t scan, bool down);

// Nhả mọi phím/nút đang giữ — gọi khi cửa sổ xem mất focus hoặc tắt bắt input.
void dh_release_all_input(void);

// Chuột tuyệt đối: toạ độ chuẩn hoá 0..65535 trong khung video.
void dh_mouse_move(int32_t nx, int32_t ny);

// Chuột tương đối — chế độ khoá chuột cho game FPS (F9): delta thô.
void dh_mouse_move_rel(int32_t dx, int32_t dy);

// Nhấn/nhả nút chuột (1 = trái, 2 = phải, 3 = giữa) tại vị trí con trỏ hiện hành.
void dh_mouse_button(int32_t button, bool down);

// Con lăn. `delta` là bội của 120 (dương = cuộn lên), như WHEEL_DELTA của Windows.
void dh_mouse_wheel(int32_t delta);

// --- Clipboard hai chiều (GĐ8) ---

// Máy này vừa copy văn bản -> gửi sang host.
void dh_set_clipboard(const char* utf8);

// Host vừa copy văn bản. Trả chuỗi RỖNG nếu không có gì mới; chuỗi tĩnh, hợp lệ tới
// lần gọi kế. Đọc-và-xoá nên poll lặp là an toàn.
const char* dh_take_remote_clipboard(void);

// --- Trạng thái để UI vẽ ---
DHPhase dh_phase(void);
// GĐ9: host có nhận điều khiển không (cờ inputAccepted trong HELLO_ACK). Đáng tin
// sau khi phiên đã đàm phán; trước đó trả true. UI giấu nút khoá chuột khi false.
bool dh_input_accepted(void);
// Dòng số liệu cho overlay (fps/kbps/RTT/e2e). Chuỗi tĩnh, hợp lệ tới lần gọi kế.
const char* dh_status_line(void);
// Lý do phiên kết thúc. Rỗng nếu chưa kết thúc.
const char* dh_end_reason(void);
uint32_t dh_video_width(void);
uint32_t dh_video_height(void);

// ===========================================================================
// Bảng phím dùng chung
// ===========================================================================

// NSEvent.keyCode -> (VK Windows, scancode PC). false = phím không dịch được, caller
// bỏ qua. Swift gọi hàm này thay vì giữ một bản sao bảng phím thứ hai — xem
// input/MacKeyMap.h về lý do chỉ được có MỘT bảng.
bool dh_map_key(uint16_t mac_key_code, int32_t* out_vk, int32_t* out_scan);

// ===========================================================================
// Quyền hệ thống (chỉ vai AGENT cần)
// ===========================================================================
bool dh_has_screen_recording(void);
bool dh_request_screen_recording(void);
void dh_open_screen_recording_settings(void);
bool dh_has_accessibility(void);
bool dh_request_accessibility(void);
void dh_open_accessibility_settings(void);

// ===========================================================================
// Vai AGENT — chia sẻ máy này
// ===========================================================================

// Một thứ máy này chia sẻ được (một cửa sổ hoặc một màn hình).
typedef struct {
    uint32_t id; // CGWindowID hoặc CGDirectDisplayID
    bool isDisplay;
    uint32_t width;
    uint32_t height;
    char name[256];
} DHShareSource;

// Trạng thái một nguồn đang chia sẻ, cho màn hình phiên.
typedef struct {
    uint8_t sourceId;
    uint32_t width;
    uint32_t height;
    bool viewerConnected;
    bool starting; // đã thêm nhưng chưa có frame đầu
    double captureFps;
    double sendFps;
    double sendKbps;
    char name[256];
} DHAgentStatus;

// Liệt kê cửa sổ + màn hình chia sẻ được. CHẶN ~2s, gọi ngoài main thread.
int dha_list_share_sources(DHShareSource* out, int capacity);

// Bắt đầu chia sẻ. CHẶN tới ~10s (đợi frame đầu), gọi ngoài main thread.
// `port` = 0 dùng mặc định 47777; cổng bận thì tự tăng dần (xem dha_port).
// `share_clipboard` MẶC ĐỊNH phải là false (GĐ9): clipboard hay chứa mật khẩu/OTP,
// người dùng phải tự tay bật.
bool dha_start(const DHShareSource* sources, int count, uint16_t port, uint32_t fps,
    uint32_t bitrate_mbps, bool allow_input, bool share_clipboard);

void dha_stop(void);
bool dha_running(void);

// Cổng THẬT đang nghe — có thể khác cổng đã xin nếu cổng đó bận.
uint16_t dha_port(void);

// Dòng trạng thái tổng. Chuỗi tĩnh, hợp lệ tới lần gọi kế.
const char* dha_status_line(void);

// Trạng thái từng nguồn. Trả số dòng đã ghi.
int dha_status(DHAgentStatus* out, int capacity);

// Địa chỉ IPv4 của máy này để đọc cho người bên kia, mỗi dòng một địa chỉ, cách nhau
// bằng '\n'. Chuỗi tĩnh, hợp lệ tới lần gọi kế.
const char* dha_local_addresses(void);

// Thêm/bớt nguồn GIỮA PHIÊN. Không chặn — thread Recv thi hành ở vòng kế tiếp.
void dha_add_source(const DHShareSource* s);
void dha_remove_source(uint8_t source_id);

#ifdef __cplusplus
}
#endif
