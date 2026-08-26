#pragma once
#include "deskhub/media/AgentTypes.h"
#include "deskhub/session/TerminalSession.h"

#include <cstdint>
#include <string>
#include <vector>

namespace deskhub::ui {

inline constexpr const char* kViewerRowLabel = "    \xE2\x86\xB3 viewer";
inline constexpr const char* kShellRowLabel = "    \xE2\x86\xB3 shell";
inline constexpr uint8_t kTerminalSourceId = 0xFF;

struct HostRow {
    bool viewer = false;
    uint8_t sourceId = 0;
    std::string viewerAddr{};
    std::string viewerName{};
    bool terminal = false;
    uint32_t termId = 0;
    TerminalState shellState = TerminalState::Live;

    bool operator==(const HostRow&) const = default;
};

std::string ViewerLabel(const std::string& name, const std::string& addr);

struct HostRowCells {
    std::string source{};
    std::string size{};
    std::string viewers{};
    std::string client{};
    std::string capture{};
    std::string send{};
    std::string mbps{};
    std::string rtt{};
    bool online = false;
};

std::vector<HostRow> BuildHostRows(const std::vector<media::AgentSourceStatus>& sources);

std::vector<HostRow> BuildHostRows(const std::vector<media::AgentSourceStatus>& sources,
    bool terminalShared, const std::vector<TerminalRecord>& shells);

const media::AgentSourceStatus* FindHostSource(
    const std::vector<media::AgentSourceStatus>& sources, uint8_t sourceId);

const TerminalRecord* FindShell(const std::vector<TerminalRecord>& shells, uint32_t termId);

HostRowCells HostRowText(const HostRow& row, const media::AgentSourceStatus& source);

HostRowCells TerminalRowText(const HostRow& row, uint16_t port,
    const std::vector<TerminalRecord>& shells);

}
