#include "deskhubp/ffi/AgentSession.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "deskhub/media/QualityPreset.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/ui/HostRows.h"
#include "deskhubp/ffi/DiscoveryFfi.h"
#include "deskhubp/ffi/FfiText.h"
#include "deskhubp/media/DisplayEnum.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/AgentLoop.h"
#include "deskhubp/session/TerminalHost.h"
#include "deskhubp/system/UiSettingsStore.h"

namespace {

std::unique_ptr<AgentLoop> g_agent;
std::unique_ptr<deskhubp::TerminalHost> g_terminal;
std::mutex g_agentMutex;
uint16_t g_port = deskhub::kDeskhubPort;

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
    uint32_t max_dim, uint16_t port, bool allow_input, const char* passcode, bool terminal) {
    if ((!sources || count <= 0) && !terminal) return false;

    std::vector<AgentSource> list;
    if (sources && count > 0) {
        list.reserve(size_t(count));
        for (int i = 0; i < count; ++i) list.push_back(ToAgentSource(sources[i]));
    }

    AgentOptions opt;
    if (fps) opt.fps = fps;
    if (bitrate_mbps) opt.bitrateMbps = bitrate_mbps;
    opt.maxDim = max_dim;
    if (port) opt.port = port;
    opt.allowInput = allow_input;
    opt.terminal = terminal;
    const std::string typedPasscode = passcode ? passcode : "";
    opt.passcode = typedPasscode.empty() || deskhub::IsValidPasscode(typedPasscode)
                       ? typedPasscode
                       : deskhubp::HostPasscode();
    {
        const deskhub::ui::UiSettings stored = deskhubp::LoadUiSettings();
        opt.bindIp = stored.bindIp;
        opt.clipboardSync = stored.clipboardSync;
        opt.deviceName = stored.deviceName;
        opt.allowNewPairings = stored.allowNewPairings;
    }

    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) {
        if (g_terminal) g_terminal->Stop();
        g_agent->Stop();
        g_agent.reset();
    }
    g_agent = std::make_unique<AgentLoop>();
    if (!g_terminal) g_terminal = std::make_unique<deskhubp::TerminalHost>();
    g_agent->SetTerminal(g_terminal.get());
    if (!g_agent->Start(list, opt)) {
        deskhubp::CopyToBuf(g_errorBuf, sizeof(g_errorBuf), g_agent->LastError());
        g_agent.reset();
        return false;
    }
    g_port = opt.port;
    if (terminal)
        g_terminal->Start(g_agent->Socket(), std::string(), deskhubp::TerminalHostCallbacks{});
    g_errorBuf[0] = '\0';
    return true;
}

void dha_stop(void) {
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) {
        if (g_terminal) g_terminal->Stop();
        g_agent->Stop();
        g_agent.reset();
    }
}

bool dha_terminal_active(void) {
    std::unique_lock<std::mutex> lk(g_agentMutex, std::try_to_lock);
    return lk.owns_lock() && g_terminal && g_terminal->Running();
}

void dha_kick_shell(uint32_t term_id) {
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_terminal) g_terminal->KickSession(term_id);
}

void dha_stop_terminal(void) {
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_terminal) g_terminal->Stop();
}

int dha_take_pairing_requests(DHPairingRequest* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    std::vector<deskhubp::PairingRequest> requests;
    {
        std::unique_lock<std::mutex> lk(g_agentMutex, std::try_to_lock);
        if (!lk.owns_lock() || !g_agent) return 0;
        requests = g_agent->TakePairingRequests(size_t(capacity));
    }
    const int count = int(requests.size()) < capacity ? int(requests.size()) : capacity;
    for (int i = 0; i < count; ++i) {
        out[i].addrPacked = requests[size_t(i)].addrPacked;
        deskhubp::CopyToBuf(out[i].shortKey, sizeof(out[i].shortKey),
            requests[size_t(i)].shortKey);
        deskhubp::CopyToBuf(out[i].name, sizeof(out[i].name), requests[size_t(i)].name);
    }
    return count;
}

void dha_answer_pairing(uint64_t addr_packed, bool allowed) {
    std::lock_guard<std::mutex> lk(g_agentMutex);
    if (g_agent) g_agent->AnswerPairing(addr_packed, allowed);
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
    std::vector<deskhub::TerminalRecord> shells;
    bool terminalOn = false;
    uint16_t port = deskhub::kDeskhubPort;
    {
        std::unique_lock<std::mutex> lk(g_agentMutex, std::try_to_lock);
        if (!lk.owns_lock() || !g_agent) return 0;
        sources = g_agent->Status();
        terminalOn = g_terminal && g_terminal->Running();
        if (terminalOn) shells = g_terminal->Sessions();
        port = g_port;
    }

    const std::vector<deskhub::ui::HostRow> rows =
        deskhub::ui::BuildHostRows(sources, terminalOn, shells);
    int written = 0;
    for (const deskhub::ui::HostRow& row : rows) {
        if (written >= capacity) break;
        deskhub::ui::HostRowCells cells;
        if (row.terminal) {
            cells = deskhub::ui::TerminalRowText(row, port, shells);
        } else {
            const AgentSourceStatus* source =
                deskhub::ui::FindHostSource(sources, row.sourceId);
            if (!source) continue;
            cells = deskhub::ui::HostRowText(row, *source);
        }
        DHHostRow& slot = out[written++];
        slot.viewer = row.viewer;
        slot.terminal = row.terminal;
        slot.sourceId = row.sourceId;
        slot.termId = row.termId;
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
