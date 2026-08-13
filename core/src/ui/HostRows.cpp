#include "deskhub/ui/HostRows.h"

#include <cstdio>

#include "deskhub/media/SourceLabel.h"
#include "deskhub/ui/Strings.h"

namespace deskhub::ui {

namespace {

std::string Decimals(double value, int places) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", places, value);
    return std::string(buf);
}

}

std::string ViewerLabel(const std::string& name, const std::string& addr) {
    if (name.empty()) return addr;
    return name + " (" + addr + ")";
}

std::vector<HostRow> BuildHostRows(const std::vector<media::AgentSourceStatus>& sources) {
    std::vector<HostRow> rows;
    for (const media::AgentSourceStatus& source : sources) {
        rows.push_back(HostRow{false, source.sourceId, {}, {}});
        for (size_t i = 0; i < source.viewerAddrs.size(); ++i) {
            const std::string name =
                i < source.viewerNames.size() ? source.viewerNames[i] : std::string();
            rows.push_back(HostRow{true, source.sourceId, source.viewerAddrs[i], name});
        }
    }
    return rows;
}

const media::AgentSourceStatus* FindHostSource(
    const std::vector<media::AgentSourceStatus>& sources, uint8_t sourceId) {
    for (const media::AgentSourceStatus& source : sources)
        if (source.sourceId == sourceId) return &source;
    return nullptr;
}

HostRowCells HostRowText(const HostRow& row, const media::AgentSourceStatus& source) {
    HostRowCells cells;
    if (row.viewer) {
        cells.source = kViewerRowLabel;
        cells.client = ViewerLabel(row.viewerName, row.viewerAddr);
        cells.online = true;
        return cells;
    }

    cells.source = source.name;
    cells.size = media::SourceSizeLabel(source.width, source.height);
    cells.viewers = std::to_string(source.viewerCount);
    cells.capture = Decimals(source.captureFps, 0);
    cells.send = Decimals(source.sendFps, 0);
    cells.mbps = Decimals(source.sendKbps / 1000.0, 1);
    cells.rtt = source.viewerConnected ? PingMs(source.rttMs) : std::string("-");
    cells.online = source.viewerConnected;
    return cells;
}

}
