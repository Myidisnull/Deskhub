#pragma once
#include "deskhub/media/AgentTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace deskhub::ui {

inline constexpr const char* kViewerRowLabel = "    \xE2\x86\xB3 viewer";

struct HostRow {
    bool viewer = false;
    uint8_t sourceId = 0;
    std::string viewerAddr{};

    bool operator==(const HostRow&) const = default;
};

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

const media::AgentSourceStatus* FindHostSource(
    const std::vector<media::AgentSourceStatus>& sources, uint8_t sourceId);

HostRowCells HostRowText(const HostRow& row, const media::AgentSourceStatus& source);

}
