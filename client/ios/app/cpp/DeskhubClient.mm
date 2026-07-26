// =============================================================================
// DeskhubClient.mm — cài đặt mặt tiền C, bọc ClientLoop và SourceQuery.
//
// Đối ứng JniBridge.cpp bên Android: một biến toàn cục giữ phiên hiện tại, mọi
// hàm facade thao tác trên đó. Obj-C++ vì cần __bridge cast (layer là kiểu Obj-C).
//
// LIÊN QUAN: DeskhubClient.h (hợp đồng), ClientLoop.h, net/SourceQuery.h
// =============================================================================
#import <AVFoundation/AVFoundation.h>

#include "DeskhubClient.h"
#include "ClientLoop.h"
#include "net/SourceQuery.h"
#include "Log.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace {

// Phiên duy nhất, y như g_client bên Android.
std::unique_ptr<ClientLoop> g_client;
std::mutex g_mutex;

// Buffer tĩnh cho chuỗi trả về (hợp lệ tới lần gọi kế). Thread-safe vì chỉ
// main thread gọi status_line/end_reason.
char g_statusBuf[256];
char g_reasonBuf[256];

constexpr uint16_t kDefaultPort = 47777;

} // namespace

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

bool dh_start(const char* address, uint8_t sourceId, uint32_t client_id,
    const char* device_name, const char* password,
    const uint8_t* device_token, int device_token_len) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client) {
        g_client->Stop();
        g_client.reset();
    }

    NetAddr addr;
    if (!ParseNetAddr(address, kDefaultPort, addr)) {
        LOGE("[Bridge] Invalid address: %s", address);
        return false;
    }

    // GĐ10: mọi trường đều được phép rỗng — khi đó phiên đi đường "chưa có mật khẩu"
    // và host sẽ hỏi (DHPhaseNeedPassword).
    ClientLoop::Credentials creds;
    creds.clientId = client_id;
    if (device_name) creds.deviceName = device_name;
    if (password) creds.password = password;
    if (device_token && device_token_len > 0)
        creds.deviceToken.assign(device_token, device_token + device_token_len);

    g_client = std::make_unique<ClientLoop>();
    if (!g_client->Start(addr, sourceId, creds)) {
        g_client.reset();
        return false;
    }
    return true;
}

void dh_submit_password(const char* password) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client && password) g_client->SubmitPassword(password);
}

int dh_take_device_token(uint8_t* out, int cap) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_client || !out || cap <= 0) return 0;
    const std::vector<uint8_t> tok = g_client->TakeDeviceToken();
    if (tok.empty()) return 0;
    // Bộ đệm quá nhỏ: trả 0 chứ KHÔNG chép một phần. Nửa cái token là token sai, mà
    // caller lại lưu nó xuống Keychain và tưởng đã nhớ được thiết bị.
    if (int(tok.size()) > cap) return 0;
    std::memcpy(out, tok.data(), tok.size());
    return int(tok.size());
}

int dh_reject_reason(void) {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_client ? int(g_client->rejectReason()) : 0;
}

void dh_stop(void) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client) {
        g_client->Stop();
        g_client.reset();
    }
}

void dh_set_layer(void* layer) {
    ClientLoop* cl = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        cl = g_client.get();
    }
    // SetLayer blocks until Decode thread acks — must not hold g_mutex during that
    // wait, otherwise poll calls (dh_phase, dh_status_line) from main thread deadlock.
    if (cl) cl->SetLayer(layer);
}

void dh_key_tap(int32_t vk, int32_t scan) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client) g_client->QueueKeyTap(vk, scan);
}

void dh_key_chord(int32_t mod_vk, int32_t mod_scan, int32_t vk, int32_t scan) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client) g_client->QueueKeyChord(mod_vk, mod_scan, vk, scan);
}

void dh_mouse_move(int32_t nx, int32_t ny) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client) g_client->QueueMouseMoveAbs(nx, ny);
}

void dh_mouse_move_rel(int32_t dx, int32_t dy) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client) g_client->QueueMouseMoveRel(dx, dy);
}

void dh_mouse_button(int32_t button, bool down) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client) g_client->QueueMouseButton(button, down);
}

void dh_char_tap(uint32_t codepoint) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client) g_client->QueueCharTap(codepoint);
}

DHPhase dh_phase(void) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_client) return DHPhaseIdle;
    return DHPhase(int(g_client->phase()));
}

const char* dh_status_line(void) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_client) {
        g_statusBuf[0] = '\0';
        return g_statusBuf;
    }
    const std::string s = g_client->StatusLine();
    std::strncpy(g_statusBuf, s.c_str(), sizeof(g_statusBuf) - 1);
    g_statusBuf[sizeof(g_statusBuf) - 1] = '\0';
    return g_statusBuf;
}

const char* dh_end_reason(void) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_client) {
        g_reasonBuf[0] = '\0';
        return g_reasonBuf;
    }
    const std::string s = g_client->EndReason();
    std::strncpy(g_reasonBuf, s.c_str(), sizeof(g_reasonBuf) - 1);
    g_reasonBuf[sizeof(g_reasonBuf) - 1] = '\0';
    return g_reasonBuf;
}

uint32_t dh_video_width(void) {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_client ? g_client->videoWidth() : 0;
}

uint32_t dh_video_height(void) {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_client ? g_client->videoHeight() : 0;
}
