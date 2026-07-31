#import <AVFoundation/AVFoundation.h>

#include "DeskhubBridge.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "deskhubp/diag/Log.h"
#include "deskhubp/session/AgentLoop.h"
#include "Permissions.h"
#include "deskhubp/ffi/FfiText.h"
#include "deskhubp/media/DisplayEnum.h"
#include "input/MacKeyMap.h"
#include "deskhubp/net/NetInfo.h"

namespace {

std::unique_ptr<AgentLoop> g_agent;
std::mutex g_agentMutex;

char g_addrBuf[1024];

}

bool dh_map_key(uint16_t mac_key_code, int32_t* out_vk, int32_t* out_scan) {
    if (!out_vk || !out_scan) return false;
    return mackeys::MacToWin(mac_key_code, *out_vk, *out_scan);
}

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

DHShareDefaults dha_default_options(void) {
    const AgentOptions defaults;
    DHShareDefaults out;
    out.fps = defaults.fps;
    out.bitrateMbps = defaults.bitrateMbps;
    out.maxDim = defaults.maxDim;
    return out;
}

int dha_list_share_sources(DHShareSource* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const std::vector<deskhub::media::ShareSource> src = deskhubp::ListDisplays();
    const int count = int(src.size()) < capacity ? int(src.size()) : capacity;
    for (int i = 0; i < count; ++i) {
        out[i].id = uint32_t(src[i].targetId);
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
    a.targetId = s.id;
    a.name = s.name;
    a.width = s.width;
    a.height = s.height;
    return a;
}
}

bool dha_start(const DHShareSource* sources, int count, uint32_t fps, uint32_t bitrate_mbps,
    uint32_t max_dim) {
    if (!sources || count <= 0) return false;

    std::vector<AgentSource> list;
    list.reserve(size_t(count));
    for (int i = 0; i < count; ++i) list.push_back(ToAgentSource(sources[i]));

    AgentOptions opt;
    if (fps) opt.fps = fps;
    if (bitrate_mbps) opt.bitrateMbps = bitrate_mbps;
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
    g_addrBuf[0] = '\0';
    std::string joined;
    for (const auto& a : ListLocalIPv4()) {
        if (!joined.empty()) joined += '\n';
        joined += a.ip + '\t' + a.name;
    }
    deskhubp::CopyToBuf(g_addrBuf, sizeof(g_addrBuf), joined);
    return g_addrBuf;
}

