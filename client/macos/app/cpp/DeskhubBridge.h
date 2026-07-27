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
// MÔ HÌNH PHIÊN — giống app Win32 (2026-07-27)
//   Vai CLIENT theo HANDLE như DeskhubApi.h bên Windows: mỗi cửa sổ xem là một
//   DHSession* riêng (một ClientLoop), mở bao nhiêu tuỳ UI — Windows mở mỗi nguồn
//   một cửa sổ và ở đây cũng vậy. Vai AGENT vẫn là MỘT biến static (g_agent): máy
//   chỉ có một phiên chia sẻ, đúng như RunAgent bên Windows.
//
// TIỀN TỐ
//   dh_*         — tiện ích dùng chung của vai client (hỏi nguồn, bảng phím, quyền).
//   dh_session_* — MỘT phiên xem, thao tác qua handle.
//   dha_*        — vai AGENT (chia sẻ máy này).
//
// ⚠ HÀM NÀO CHẶN
//   dh_list_sources        ~3 giây (hỏi host qua UDP)
//   dh_session_start       ~1 giây (mở socket + thread)
//   dha_list_share_sources ~2 giây (hỏi ScreenCaptureKit)
//   dha_start              tới ~10 giây (đợi frame đầu của từng nguồn)
//   Các hàm đó PHẢI gọi ngoài main thread (Swift: Task.detached). Mọi hàm còn lại an
//   toàn và nhanh trên main thread.
//
// LIÊN QUAN: DeskhubBridge.mm (cài đặt), swift/DeskhubClient.swift +
//            swift/DeskhubAgent.swift (bọc Swift), ClientLoop.h,
//            AgentLoop.h
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

// Một phiên xem (một ClientLoop). Đối ứng DhClientHandle của DeskhubApi.h bên
// Windows — mỗi cửa sổ xem cầm một handle riêng, mở nhiều phiên song song được.
typedef struct DHSession DHSession;

// Bắt đầu phiên xem `address` (CHỈ địa chỉ IP — cổng cố định 47777), nguồn
// `sourceId`. Trả NULL nếu địa chỉ sai / không mở được phiên. CHẶN ~1s, gọi ngoài
// main thread. Giải phóng bằng dh_session_stop.
DHSession* dh_session_start(const char* address, uint8_t sourceId);

// Dừng phiên, join thread, giải phóng handle. `s` không dùng được sau lời gọi này.
void dh_session_stop(DHSession* s);

// Giao/thu hồi layer. `layer` là AVSampleBufferDisplayLayer* (__bridge void*), hoặc
// NULL khi cửa sổ đóng / view biến mất. CHẶN main thread cho tới khi thread Decode
// xác nhận đã buông layer cũ.
void dh_session_set_layer(DHSession* s, void* layer);

// --- Kênh input. Chỉ có tác dụng khi phiên đang STREAMING. ---

// Nhấn/nhả một phím vật lý. `vk`/`scan` do dh_map_key dịch từ NSEvent.keyCode.
void dh_session_key(DHSession* s, int32_t vk, int32_t scan, bool down);

// Nhả mọi phím/nút đang giữ — gọi khi cửa sổ xem mất focus hoặc tắt bắt input.
void dh_session_release_all_input(DHSession* s);

// Chuột tuyệt đối: toạ độ chuẩn hoá 0..65535 trong khung video.
void dh_session_mouse_move(DHSession* s, int32_t nx, int32_t ny);

// Chuột tương đối — chế độ khoá chuột cho game FPS (F9): delta thô.
void dh_session_mouse_move_rel(DHSession* s, int32_t dx, int32_t dy);

// Nhấn/nhả nút chuột (1 = trái, 2 = phải, 3 = giữa) tại vị trí con trỏ hiện hành.
void dh_session_mouse_button(DHSession* s, int32_t button, bool down);

// Con lăn. `delta` là bội của 120 (dương = cuộn lên), như WHEEL_DELTA của Windows.
void dh_session_mouse_wheel(DHSession* s, int32_t delta);

// --- Trạng thái để UI vẽ ---
DHPhase dh_session_phase(DHSession* s);
// Dòng số liệu cho thanh trên (fps/kbps/RTT/e2e). Chuỗi tĩnh, hợp lệ tới lần gọi kế.
const char* dh_session_status_line(DHSession* s);
// Lý do phiên kết thúc. Rỗng nếu chưa kết thúc.
const char* dh_session_end_reason(DHSession* s);
uint32_t dh_session_video_width(DHSession* s);
uint32_t dh_session_video_height(DHSession* s);

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
void dh_open_screen_recording_settings(void);
bool dh_has_accessibility(void);
void dh_open_accessibility_settings(void);

// ===========================================================================
// Vai AGENT — chia sẻ máy này
// ===========================================================================

// Một màn hình máy này chia sẻ được (share theo cửa sổ đã bỏ 2026-07-27).
typedef struct {
    uint32_t id; // CGDirectDisplayID
    uint32_t width;
    uint32_t height;
    char name[256];
} DHShareSource;

// Trạng thái một nguồn đang chia sẻ, cho màn hình phiên. Đối ứng SessionSourceRow
// bên Windows — cùng các trường có cấu trúc (viewer addr + RTT có từ 2026-07-27).
typedef struct {
    uint8_t sourceId;
    uint32_t width;
    uint32_t height;
    bool viewerConnected;
    double captureFps;
    double sendFps;
    double sendKbps;
    uint32_t rttMs;      // từ FEEDBACK của client; 0 = chưa có số
    char viewerAddr[64]; // "ip:port" của client đang xem, rỗng nếu không có
    char name[256];
} DHAgentStatus;

// Liệt kê màn hình chia sẻ được. CHẶN ~2s, gọi ngoài main thread.
int dha_list_share_sources(DHShareSource* out, int capacity);

// Bắt đầu chia sẻ. CHẶN tới ~10s (đợi frame đầu), gọi ngoài main thread.
// KHÔNG có tham số cổng và cũng KHÔNG có tham số "cho phép điều khiển": cổng luôn
// là 47777 (kDeskhubPort, net/UdpSocket.h) và chuột/bàn phím luôn được chia sẻ.
// false = cổng đã bị chiếm, thiếu quyền, hoặc không nguồn nào lên hình.
bool dha_start(const DHShareSource* sources, int count, uint32_t fps, uint32_t bitrate_mbps);

void dha_stop(void);
bool dha_running(void);

// Trạng thái từng nguồn. Trả số dòng đã ghi.
int dha_status(DHAgentStatus* out, int capacity);

// Địa chỉ IPv4 của máy này để hiện ở màn chính (giống hộp Host mode bên Windows),
// mỗi dòng một địa chỉ dạng "ip\ttên card", cách nhau bằng '\n'. Gọi được BẤT KỲ
// lúc nào, kể cả khi chưa chia sẻ. Chuỗi tĩnh, hợp lệ tới lần gọi kế.
const char* dha_local_addresses(void);

// KHÔNG có dha_add_source/dha_remove_source (bỏ 2026-07-27): phiên chia sẻ tất cả
// màn hình và danh sách chốt lúc dha_start, nên không có lệnh nào đổi nó giữa chừng.

#ifdef __cplusplus
}
#endif
