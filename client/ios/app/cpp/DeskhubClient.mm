#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>

#include "DeskhubClient.h"
#include "ClientLoop.h"
#include "deskhubp/diag/Log.h"

#include <cstring>
#include <memory>
#include <mutex>

namespace {

std::shared_ptr<ClientLoop> g_client;
std::mutex g_mutex;

char g_statusBuf[256];
char g_reasonBuf[256];

}

namespace {

void DeviceScreenPixels(uint32_t& w, uint32_t& h) {
    w = h = 0;
    UIScreen* s = UIScreen.mainScreen;
    if (!s) return;
    const CGRect r = s.nativeBounds;
    if (r.size.width <= 0 || r.size.height <= 0) return;
    w = uint32_t(r.size.width);
    h = uint32_t(r.size.height);
}

}

bool dh_start(const char* address, uint8_t sourceId) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client) {
        g_client->Stop();
        g_client.reset();
    }

    NetAddr addr;
    if (!ParseNetAddr(address, addr)) {
        LOGE("[Bridge] Invalid address: %s", address);
        return false;
    }

    uint32_t sw = 0, sh = 0;
    DeviceScreenPixels(sw, sh);

    g_client = std::make_shared<ClientLoop>();
    if (!g_client->Start(addr, sourceId, sw, sh)) {
        g_client.reset();
        return false;
    }
    return true;
}

void dh_stop(void) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_client) {
        g_client->Stop();
        g_client.reset();
    }
}

void dh_set_layer(void* layer) {
    std::shared_ptr<ClientLoop> cl;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        cl = g_client;
    }
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

