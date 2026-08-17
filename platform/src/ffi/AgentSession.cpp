#include "deskhubp/ffi/AgentSession.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "deskhub/crypto/KeyCodec.h"
#include "deskhub/media/QualityPreset.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/ui/HostRows.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/StallLog.h"
#include "deskhubp/ffi/DiscoveryFfi.h"
#include "deskhubp/ffi/FfiText.h"
#include "deskhubp/media/DisplayEnum.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/AgentLoop.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/Random.h"
#include "deskhubp/system/SessionCrypto.h"
#include "deskhubp/system/UiSettingsStore.h"

namespace {

std::unique_ptr<AgentLoop> g_agent;
std::mutex g_agentMutex;

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
    uint32_t max_dim, uint16_t port, bool allow_input, const char* passcode) {
    if (!sources || count <= 0) return false;

    std::vector<AgentSource> list;
    list.reserve(size_t(count));
    for (int i = 0; i < count; ++i) list.push_back(ToAgentSource(sources[i]));

    AgentOptions opt;
    if (fps) opt.fps = fps;
    if (bitrate_mbps) opt.bitrateMbps = bitrate_mbps;
    opt.maxDim = max_dim;
    if (port) opt.port = port;
    opt.allowInput = allow_input;
    opt.passcode = passcode && deskhub::IsValidPasscode(passcode) ? std::string(passcode)
                                                                  : deskhubp::HostPasscode();
    {
        deskhub::ui::UiSettings stored = deskhubp::LoadUiSettings();
        opt.bindIp = stored.bindIp;
        opt.clipboardSync = stored.clipboardSync;
        if (!deskhubp::ApplyEncryptToAgentOptions(stored, opt)) return false;
    }

    LOGI("[UI] Share start: %d source(s), %u fps, %u Mbps, port %u.", count, opt.fps,
        opt.bitrateMbps, unsigned(opt.port));

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
    LOGI("[UI] Share stop requested.");
    const uint64_t t0 = NowUs();
    deskhubp::StopAnrWatch watch("ui", "dha_stop");
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) {
        g_agent->Stop();
        g_agent.reset();
    }
    deskhubp::LogStopPhase("ui", "dha_stop", t0);
}

void dha_stop_source(uint8_t source_id) {
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) g_agent->StopSource(source_id);
}

void dha_kick_viewer(uint8_t source_id, const char* viewer_addr) {
    if (!viewer_addr) return;
    NetAddr addr{};
    if (!ParseNetAddr(viewer_addr, addr)) return;
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) g_agent->KickViewer(source_id, addr.Pack());
}

bool dha_running(void) {
    std::unique_lock<std::mutex> lk(g_agentMutex, std::try_to_lock);
    return lk.owns_lock() && g_agent && g_agent->running();
}

int dha_host_rows(DHHostRow* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    std::vector<AgentSourceStatus> sources;
    {
        std::unique_lock<std::mutex> lk(g_agentMutex, std::try_to_lock);
        if (!lk.owns_lock() || !g_agent) return 0;
        sources = g_agent->Status();
    }

    const std::vector<deskhub::ui::HostRow> rows = deskhub::ui::BuildHostRows(sources);
    int written = 0;
    for (const deskhub::ui::HostRow& row : rows) {
        if (written >= capacity) break;
        const AgentSourceStatus* source = deskhub::ui::FindHostSource(sources, row.sourceId);
        if (!source) continue;

        const deskhub::ui::HostRowCells cells = deskhub::ui::HostRowText(row, *source);
        DHHostRow& slot = out[written++];
        slot.viewer = row.viewer;
        slot.sourceId = row.sourceId;
        slot.online = cells.online;
        deskhubp::CopyToBuf(slot.viewerAddr, sizeof(slot.viewerAddr), row.viewerAddr);
        deskhubp::CopyToBuf(slot.source, sizeof(slot.source), cells.source);
        deskhubp::CopyToBuf(slot.size, sizeof(slot.size), cells.size);
        deskhubp::CopyToBuf(slot.viewers, sizeof(slot.viewers), cells.viewers);
        deskhubp::CopyToBuf(slot.client, sizeof(slot.client), cells.client);
        deskhubp::CopyToBuf(slot.capture, sizeof(slot.capture), cells.capture);
        deskhubp::CopyToBuf(slot.send, sizeof(slot.send), cells.send);
        deskhubp::CopyToBuf(slot.mbps, sizeof(slot.mbps), cells.mbps);
        deskhubp::CopyToBuf(slot.rtt, sizeof(slot.rtt), cells.rtt);
    }
    return written;
}

const char* dha_last_error(void) {
    std::unique_lock<std::mutex> lk(g_agentMutex, std::try_to_lock);
    if (lk.owns_lock() && g_agent)
        deskhubp::CopyToBuf(g_errorBuf, sizeof(g_errorBuf), g_agent->LastError());
    return g_errorBuf;
}

const char* dha_local_addresses(void) {
    return dh_local_addresses();
}

void dha_clip_offer(const char* text) {
    if (!text || !*text) return;
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) g_agent->OfferLocalClipboard(text);
}

int dha_clip_take(char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    out[0] = '\0';
    std::unique_lock<std::mutex> lk(g_agentMutex, std::try_to_lock);
    if (!lk.owns_lock() || !g_agent) return 0;
    const std::optional<std::string> text = g_agent->TakeRemoteClipboard();
    if (!text) return 0;
    deskhubp::CopyToBuf(out, size_t(capacity), *text);
    return int(std::strlen(out));
}

int dha_bind_warning(char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    out[0] = '\0';
    std::unique_lock<std::mutex> lk(g_agentMutex, std::try_to_lock);
    if (lk.owns_lock() && g_agent)
        deskhubp::CopyToBuf(out, size_t(capacity), g_agent->BindWarning());
    return int(std::strlen(out));
}
