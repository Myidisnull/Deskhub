// =============================================================================
// DeskhubBridge.mm — cài đặt mặt tiền C, bọc ClientLoop, AgentLoop và SourceQuery.
//
// Obj-C++ vì cần __bridge cast (layer là kiểu Obj-C) và vì Permissions/SourceEnum là
// .mm. Hai biến toàn cục giữ hai vai; mỗi vai một mutex riêng để chia sẻ và xem
// không chặn nhau.
//
// ⚠ VÌ SAO KHÔNG GIỮ KHOÁ TRONG dh_set_layer
//   SetLayer CHẶN tới khi thread Decode xác nhận. Giữ g_clientMutex suốt lúc đó thì
//   mọi lời gọi poll từ main thread (dh_phase, dh_status_line) sẽ tự khoá lẫn nhau
//   và app treo. Lấy con trỏ dưới khoá rồi NHẢ khoá trước khi chờ — cùng cách bản
//   iOS làm, và đây là lỗi đã từng gặp thật nên đừng "dọn dẹp" nó.
//
// LIÊN QUAN: DeskhubBridge.h (hợp đồng + hàm nào chặn), ClientLoop.h,
//            AgentLoop.h, net/SourceQuery.h,
//            client/ios/app/cpp/DeskhubClient.mm (bản song song)
// =============================================================================
#import <AVFoundation/AVFoundation.h>

#include "DeskhubBridge.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Log.h"
#include "AgentLoop.h"
#include "Permissions.h"
#include "capture/SourceEnum.h"
#include "ClientLoop.h"
#include "input/MacKeyMap.h"
#include "net/SourceQuery.h"

namespace {

std::unique_ptr<ClientLoop> g_client;
std::mutex g_clientMutex;

std::unique_ptr<AgentLoop> g_agent;
std::mutex g_agentMutex;

constexpr uint16_t kDefaultPort = 47777;

// Bộ đệm tĩnh cho chuỗi trả về (hợp lệ tới lần gọi kế). An toàn vì mọi hàm trả chuỗi
// đều được gọi từ main thread — Swift poll trạng thái trên MainActor.
char g_statusBuf[256];
char g_reasonBuf[256];
char g_agentStatusBuf[256];
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
    if (!ParseNetAddr(address, kDefaultPort, addr)) {
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

bool dh_start(const char* address, uint8_t sourceId) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (g_client) {
        g_client->Stop();
        g_client.reset();
    }

    NetAddr addr;
    if (!ParseNetAddr(address, kDefaultPort, addr)) {
        LOGE("[Bridge] Invalid address: %s", address);
        return false;
    }

    g_client = std::make_unique<ClientLoop>();
    if (!g_client->Start(addr, sourceId)) {
        g_client.reset();
        return false;
    }
    return true;
}

void dh_stop(void) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (g_client) {
        g_client->Stop();
        g_client.reset();
    }
}

void dh_set_layer(void* layer) {
    ClientLoop* cl = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_clientMutex);
        cl = g_client.get();
    }
    // KHÔNG giữ khoá khi chờ — xem cảnh báo ở đầu file.
    if (cl) cl->SetLayer(layer);
}

void dh_key(int32_t vk, int32_t scan, bool down) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (g_client) g_client->QueueKey(vk, scan, down);
}

void dh_release_all_input(void) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (g_client) g_client->ReleaseAllInput();
}

void dh_mouse_move(int32_t nx, int32_t ny) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (g_client) g_client->QueueMouseMoveAbs(nx, ny);
}

void dh_mouse_move_rel(int32_t dx, int32_t dy) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (g_client) g_client->QueueMouseMoveRel(dx, dy);
}

void dh_mouse_button(int32_t button, bool down) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (g_client) g_client->QueueMouseButton(button, down);
}

void dh_mouse_wheel(int32_t delta) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (g_client) g_client->QueueMouseWheel(delta);
}

DHPhase dh_phase(void) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (!g_client) return DHPhaseIdle;
    return DHPhase(int(g_client->phase()));
}

bool dh_input_accepted(void) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    return g_client ? g_client->InputAccepted() : true;
}

const char* dh_status_line(void) {
    g_statusBuf[0] = '\0';
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (g_client) CopyToBuf(g_statusBuf, sizeof(g_statusBuf), g_client->StatusLine());
    return g_statusBuf;
}

const char* dh_end_reason(void) {
    g_reasonBuf[0] = '\0';
    std::lock_guard<std::mutex> lk(g_clientMutex);
    if (g_client) CopyToBuf(g_reasonBuf, sizeof(g_reasonBuf), g_client->EndReason());
    return g_reasonBuf;
}

uint32_t dh_video_width(void) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    return g_client ? g_client->videoWidth() : 0;
}

uint32_t dh_video_height(void) {
    std::lock_guard<std::mutex> lk(g_clientMutex);
    return g_client ? g_client->videoHeight() : 0;
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
bool dh_request_screen_recording(void) {
    return macperm::RequestScreenRecording();
}
void dh_open_screen_recording_settings(void) {
    macperm::OpenScreenRecordingSettings();
}
bool dh_has_accessibility(void) {
    return macperm::HasAccessibility();
}
bool dh_request_accessibility(void) {
    return macperm::RequestAccessibility();
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

bool dha_start(const DHShareSource* sources, int count, uint16_t port, uint32_t fps,
    uint32_t bitrate_mbps, bool allow_input) {
    if (!sources || count <= 0) return false;

    std::vector<AgentSource> list;
    list.reserve(size_t(count));
    for (int i = 0; i < count; ++i) list.push_back(ToAgentSource(sources[i]));

    AgentOptions opt;
    opt.port = port ? port : kDefaultPort;
    opt.fps = fps ? fps : 60;
    opt.bitrateMbps = bitrate_mbps ? bitrate_mbps : 20;
    opt.allowInput = allow_input;

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

uint16_t dha_port(void) {
    std::lock_guard<std::mutex> lk(g_agentMutex);
    return g_agent ? g_agent->port() : 0;
}

const char* dha_status_line(void) {
    g_agentStatusBuf[0] = '\0';
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) CopyToBuf(g_agentStatusBuf, sizeof(g_agentStatusBuf), g_agent->StatusLine());
    return g_agentStatusBuf;
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
        out[i].starting = rows[i].starting;
        out[i].captureFps = rows[i].captureFps;
        out[i].sendFps = rows[i].sendFps;
        out[i].sendKbps = rows[i].sendKbps;
        std::strncpy(out[i].name, rows[i].name.c_str(), sizeof(out[i].name) - 1);
        out[i].name[sizeof(out[i].name) - 1] = '\0';
    }
    return count;
}

const char* dha_local_addresses(void) {
    g_addrBuf[0] = '\0';
    std::vector<std::string> addrs;
    {
        std::lock_guard<std::mutex> lk(g_agentMutex);
        if (g_agent) addrs = g_agent->LocalAddresses();
    }
    std::string joined;
    for (const std::string& a : addrs) {
        if (!joined.empty()) joined += '\n';
        joined += a;
    }
    CopyToBuf(g_addrBuf, sizeof(g_addrBuf), joined);
    return g_addrBuf;
}

void dha_add_source(const DHShareSource* s) {
    if (!s) return;
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) g_agent->AddSource(ToAgentSource(*s));
}

void dha_remove_source(uint8_t source_id) {
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) g_agent->RemoveSource(source_id);
}
