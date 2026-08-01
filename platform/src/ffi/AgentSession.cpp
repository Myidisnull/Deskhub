#include "deskhubp/ffi/AgentSession.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "deskhub/media/QualityPreset.h"
#include "deskhub/media/SourceLabel.h"
#include "deskhubp/ffi/FfiText.h"
#include "deskhubp/media/DisplayEnum.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/session/AgentLoop.h"

namespace {

std::unique_ptr<AgentLoop> g_agent;
std::mutex g_agentMutex;

char g_addrBuf[1024];
char g_errorBuf[512];

AgentSource ToAgentSource(const DHShareSource& s) {
    AgentSource a;
    a.targetId = s.id;
    a.name = s.name;
    a.width = s.width;
    a.height = s.height;
    return a;
}

}

DHShareDefaults dha_default_options(void) {
    const AgentOptions defaults;
    return DHShareDefaults{defaults.fps, defaults.bitrateMbps, defaults.maxDim};
}

int dha_quality_presets(DHQualityPreset* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const auto& presets = deskhub::media::kQualityPresets;
    const int count = int(presets.size()) < capacity ? int(presets.size()) : capacity;
    for (int i = 0; i < count; ++i) {
        deskhubp::CopyToBuf(out[i].label, sizeof(out[i].label), presets[size_t(i)].label);
        out[i].maxDim = presets[size_t(i)].maxDim;
    }
    return count;
}

int dha_list_share_sources(DHShareSource* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const std::vector<deskhub::media::ShareSource> src = deskhubp::ListDisplays();
    const int count = int(src.size()) < capacity ? int(src.size()) : capacity;
    for (int i = 0; i < count; ++i) {
        out[i].id = uint32_t(src[size_t(i)].targetId);
        out[i].width = src[size_t(i)].width;
        out[i].height = src[size_t(i)].height;
        deskhubp::CopyToBuf(out[i].name, sizeof(out[i].name), src[size_t(i)].name);
    }
    return count;
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
        deskhubp::CopyToBuf(g_errorBuf, sizeof(g_errorBuf), g_agent->LastError());
        g_agent.reset();
        return false;
    }
    g_errorBuf[0] = '\0';
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
        const AgentSourceStatus& row = rows[size_t(i)];
        out[i].sourceId = row.sourceId;
        out[i].width = row.width;
        out[i].height = row.height;
        out[i].viewerConnected = row.viewerConnected;
        out[i].zeroCopy = row.zeroCopy;
        out[i].captureFps = row.captureFps;
        out[i].sendFps = row.sendFps;
        out[i].sendKbps = row.sendKbps;
        out[i].rttMs = row.rttMs;
        deskhubp::CopyToBuf(out[i].viewerAddr, sizeof(out[i].viewerAddr), row.viewerAddr);
        deskhubp::CopyToBuf(out[i].name, sizeof(out[i].name), row.name);
        deskhubp::CopyToBuf(out[i].label, sizeof(out[i].label),
            deskhub::media::SharedSourceLabel(row.name, row.width, row.height,
                row.viewerConnected));
    }
    return count;
}

const char* dha_last_error(void) {
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) deskhubp::CopyToBuf(g_errorBuf, sizeof(g_errorBuf), g_agent->LastError());
    return g_errorBuf;
}

const char* dha_local_addresses(void) {
    std::string joined;
    for (const auto& a : ListLocalIPv4()) {
        if (!joined.empty()) joined += '\n';
        joined += a.ip + '\t' + a.name;
    }
    deskhubp::CopyToBuf(g_addrBuf, sizeof(g_addrBuf), joined);
    return g_addrBuf;
}
