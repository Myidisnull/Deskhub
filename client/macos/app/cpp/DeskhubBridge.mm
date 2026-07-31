// =============================================================================
// DeskhubBridge.mm — cài đặt mặt tiền C, bọc ClientLoop, AgentLoop và SourceQuery.
//
// Obj-C++ vì cần __bridge cast (layer là kiểu Obj-C) và vì Permissions/SourceEnum là
// .mm.
//
// VAI CLIENT THEO HANDLE (2026-07-27, giống ClientApi.cpp bên Windows)
//   DHSession bọc một ClientLoop; Swift cầm handle và sở hữu vòng đời — không còn
//   g_client toàn cục, nên KHÔNG cần mutex: mọi lời gọi trên một handle đều từ main
//   thread (poll + input) trừ dh_session_start/stop chạy trong Task.detached, và
//   Swift bảo đảm không gọi gì sau stop.
//
// VAI AGENT vẫn là MỘT biến static + mutex: máy chỉ có một phiên chia sẻ.
//
// LIÊN QUAN: DeskhubBridge.h (hợp đồng + hàm nào chặn), ClientLoop.h,
//            AgentLoop.h, net/SourceQuery.h,
//            client/windows/cpp/DeskhubApi.h (bản đối ứng),
//            client/ios/app/cpp/DeskhubClient.mm (bản song song một-phiên)
// =============================================================================
#import <AVFoundation/AVFoundation.h>

#include "DeskhubBridge.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "deskhubp/Log.h"
#include "AgentLoop.h"
#include "Permissions.h"
#include "capture/SourceEnum.h"
#include "ClientLoop.h"
#include "input/MacKeyMap.h"
#include "net/NetInfo.h"
#include "deskhubp/SourceQuery.h"

// Một phiên xem = một ClientLoop. Định nghĩa ngoài namespace ẩn danh vì tên
// DHSession thuộc hợp đồng C của header.
struct DHSession {
    ClientLoop loop;
};

namespace {

std::unique_ptr<AgentLoop> g_agent;
std::mutex g_agentMutex;

// Bộ đệm tĩnh cho chuỗi trả về (hợp lệ tới lần gọi kế). An toàn vì mọi hàm trả chuỗi
// đều được gọi từ main thread — Swift poll trạng thái trên MainActor, và copy ngay
// sang String trước lời gọi kế tiếp (kể cả khi có nhiều phiên xem).
char g_statusBuf[256];
char g_reasonBuf[256];
char g_addrBuf[1024];

void CopyToBuf(char* dst, size_t cap, const std::string& s) {
    const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

} // namespace

// ===========================================================================
// Vai CLIENT
// ===========================================================================

int dh_list_sources(const char* address, DHSourceInfo* out, int capacity) {
    if (!address || !out || capacity <= 0) return 0;

    NetAddr addr;
    if (!ParseNetAddr(address, addr)) {
        LOGE("[Bridge] Invalid address: %s", address);
        return 0;
    }

    std::vector<deskhub::SourceInfo> sources;
    if (!QuerySources(addr, sources)) return 0;

    const int count = int(sources.size()) < capacity ? int(sources.size()) : capacity;
    for (int i = 0; i < count; ++i) {
        out[i].sourceId = sources[i].sourceId;
        out[i].width = sources[i].width;
        out[i].height = sources[i].height;
        std::strncpy(out[i].name, sources[i].name.c_str(), sizeof(out[i].name) - 1);
        out[i].name[sizeof(out[i].name) - 1] = '\0';
    }
    return count;
}

namespace {

// Màn hình LỚN NHẤT đang gắn vào máy này, tính bằng PIXEL. Đi vào HELLO để host co
// luồng cho vừa (deskhub::Hello::maxWidth).
//
// Lấy cái LỚN NHẤT chứ không phải màn chính: laptop cắm màn ngoài 4K thì người dùng
// gần như chắc chắn xem ở màn ngoài, và khai theo panel laptop sẽ khoá luồng ở một
// độ phân giải thấp hơn thứ họ đang nhìn.
//
// CoreGraphics chứ không phải NSScreen, có chủ ý: hàm này chạy từ Task.detached (xem
// đầu file), tức KHÔNG phải main thread, mà AppKit thì không hứa gì ngoài main thread.
// CGDisplayModeGetPixelWidth cho thẳng số pixel thật của chế độ đang chạy — không
// phải nhân backingScaleFactor như đường CGDisplayPixelsWide đời cũ.
void LargestScreenPixels(uint32_t& outW, uint32_t& outH) {
    outW = outH = 0;
    CGDirectDisplayID ids[16];
    uint32_t n = 0;
    if (CGGetActiveDisplayList(16, ids, &n) != kCGErrorSuccess || !n) return;
    uint64_t bestArea = 0;
    for (uint32_t i = 0; i < n; ++i) {
        CGDisplayModeRef m = CGDisplayCopyDisplayMode(ids[i]);
        if (!m) continue;
        const uint64_t w = CGDisplayModeGetPixelWidth(m), h = CGDisplayModeGetPixelHeight(m);
        CGDisplayModeRelease(m);
        if (!w || !h || w * h <= bestArea) continue;
        bestArea = w * h;
        outW = uint32_t(w);
        outH = uint32_t(h);
    }
}

} // namespace

DHSession* dh_session_start(const char* address, uint8_t sourceId) {
    if (!address) return nullptr;
    NetAddr addr;
    if (!ParseNetAddr(address, addr)) {
        LOGE("[Bridge] Invalid address: %s", address);
        return nullptr;
    }
    uint32_t sw = 0, sh = 0;
    LargestScreenPixels(sw, sh);
    auto s = std::make_unique<DHSession>();
    if (!s->loop.Start(addr, sourceId, sw, sh)) return nullptr;
    return s.release();
}

void dh_session_stop(DHSession* s) {
    if (!s) return;
    s->loop.Stop();
    delete s;
}

void dh_session_set_layer(DHSession* s, void* layer) {
    // SetLayer CHẶN tới khi thread Decode xác nhận — không sao: chỉ handle này chờ,
    // các phiên khác không liên quan (ngày trước g_client + mutex chung thì chỗ này
    // từng làm mọi lời poll tự khoá lẫn nhau).
    if (s) s->loop.SetLayer(layer);
}

void dh_session_key(DHSession* s, int32_t vk, int32_t scan, bool down) {
    if (s) s->loop.QueueKey(vk, scan, down);
}

void dh_session_release_all_input(DHSession* s) {
    if (s) s->loop.ReleaseAllInput();
}

void dh_session_mouse_move(DHSession* s, int32_t nx, int32_t ny) {
    if (s) s->loop.QueueMouseMoveAbs(nx, ny);
}

void dh_session_mouse_move_rel(DHSession* s, int32_t dx, int32_t dy) {
    if (s) s->loop.QueueMouseMoveRel(dx, dy);
}

void dh_session_mouse_button(DHSession* s, int32_t button, bool down) {
    if (s) s->loop.QueueMouseButton(button, down);
}

void dh_session_mouse_wheel(DHSession* s, int32_t delta) {
    if (s) s->loop.QueueMouseWheel(delta);
}

DHPhase dh_session_phase(DHSession* s) {
    if (!s) return DHPhaseIdle;
    return DHPhase(int(s->loop.phase()));
}

const char* dh_session_status_line(DHSession* s) {
    g_statusBuf[0] = '\0';
    if (s) CopyToBuf(g_statusBuf, sizeof(g_statusBuf), s->loop.StatusLine());
    return g_statusBuf;
}

const char* dh_session_end_reason(DHSession* s) {
    g_reasonBuf[0] = '\0';
    if (s) CopyToBuf(g_reasonBuf, sizeof(g_reasonBuf), s->loop.EndReason());
    return g_reasonBuf;
}

uint32_t dh_session_video_width(DHSession* s) {
    return s ? s->loop.videoWidth() : 0;
}

uint32_t dh_session_video_height(DHSession* s) {
    return s ? s->loop.videoHeight() : 0;
}

// ===========================================================================
// Bảng phím dùng chung
// ===========================================================================

bool dh_map_key(uint16_t mac_key_code, int32_t* out_vk, int32_t* out_scan) {
    if (!out_vk || !out_scan) return false;
    return mackeys::MacToWin(mac_key_code, *out_vk, *out_scan);
}

// ===========================================================================
// Quyền hệ thống
// ===========================================================================

bool dh_has_screen_recording(void) {
    return macperm::HasScreenRecording();
}
void dh_open_screen_recording_settings(void) {
    macperm::OpenScreenRecordingSettings();
}
bool dh_has_accessibility(void) {
    return macperm::HasAccessibility();
}
void dh_open_accessibility_settings(void) {
    macperm::OpenAccessibilitySettings();
}

// ===========================================================================
// Vai AGENT
// ===========================================================================

int dha_list_share_sources(DHShareSource* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const std::vector<ShareSource> src = GetShareSources();
    const int count = int(src.size()) < capacity ? int(src.size()) : capacity;
    for (int i = 0; i < count; ++i) {
        out[i].id = src[i].displayId;
        out[i].width = src[i].width;
        out[i].height = src[i].height;
        std::strncpy(out[i].name, src[i].name.c_str(), sizeof(out[i].name) - 1);
        out[i].name[sizeof(out[i].name) - 1] = '\0';
    }
    return count;
}

namespace {
AgentSource ToAgentSource(const DHShareSource& s) {
    AgentSource a;
    a.displayId = s.id;
    a.name = s.name;
    return a;
}
} // namespace

bool dha_start(const DHShareSource* sources, int count, uint32_t fps, uint32_t bitrate_mbps,
    uint32_t max_dim) {
    if (!sources || count <= 0) return false;

    std::vector<AgentSource> list;
    list.reserve(size_t(count));
    for (int i = 0; i < count; ++i) list.push_back(ToAgentSource(sources[i]));

    AgentOptions opt;
    opt.fps = fps ? fps : 60;
    opt.bitrateMbps = bitrate_mbps ? bitrate_mbps : 20;
    // 0 ở đây là LỰA CHỌN THẬT ("gửi native"), không phải "chưa đặt" — nên không có
    // phép thay-0-bằng-mặc-định như hai tham số trên.
    opt.maxDim = max_dim;

    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) {
        g_agent->Stop();
        g_agent.reset();
    }
    g_agent = std::make_unique<AgentLoop>();
    if (!g_agent->Start(list, opt)) {
        g_agent.reset();
        return false;
    }
    return true;
}

void dha_stop(void) {
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) {
        g_agent->Stop();
        g_agent.reset();
    }
}

bool dha_running(void) {
    std::lock_guard<std::mutex> lk(g_agentMutex);
    return g_agent && g_agent->running();
}

int dha_status(DHAgentStatus* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    std::vector<AgentSourceStatus> rows;
    {
        std::lock_guard<std::mutex> lk(g_agentMutex);
        if (!g_agent) return 0;
        rows = g_agent->Status();
    }
    const int count = int(rows.size()) < capacity ? int(rows.size()) : capacity;
    for (int i = 0; i < count; ++i) {
        out[i].sourceId = rows[i].sourceId;
        out[i].width = rows[i].width;
        out[i].height = rows[i].height;
        out[i].viewerConnected = rows[i].viewerConnected;
        out[i].captureFps = rows[i].captureFps;
        out[i].sendFps = rows[i].sendFps;
        out[i].sendKbps = rows[i].sendKbps;
        out[i].rttMs = rows[i].rttMs;
        std::strncpy(out[i].viewerAddr, rows[i].viewerAddr.c_str(),
            sizeof(out[i].viewerAddr) - 1);
        out[i].viewerAddr[sizeof(out[i].viewerAddr) - 1] = '\0';
        std::strncpy(out[i].name, rows[i].name.c_str(), sizeof(out[i].name) - 1);
        out[i].name[sizeof(out[i].name) - 1] = '\0';
    }
    return count;
}

const char* dha_local_addresses(void) {
    // Hỏi thẳng hệ thống thay vì AgentLoop: màn chính hiện IP TRƯỚC khi chia sẻ,
    // đúng như hộp Host mode của MainMenuWindow bên Windows.
    g_addrBuf[0] = '\0';
    std::string joined;
    for (const auto& a : ListLocalIPv4()) {
        if (!joined.empty()) joined += '\n';
        joined += a.ip + '\t' + a.name;
    }
    CopyToBuf(g_addrBuf, sizeof(g_addrBuf), joined);
    return g_addrBuf;
}
