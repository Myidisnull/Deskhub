#include "deskhub/ui/HostRows.h"

#include <cstdio>
#include <utility>

#include "deskhub/media/SourceLabel.h"
#include "deskhub/ui/Strings.h"

namespace deskhub::ui {

namespace {

std::string Decimals(double value, int places) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", places, value);
    return std::string(buf);
}

std::string TransferPercent(const TransferRecord& transfer) {
    if (transfer.batchSize == 0) return "100%";
    return std::to_string(transfer.batchBytes * 100 / transfer.batchSize) + "%";
}

std::vector<HostRow> AppendFileRows(std::vector<HostRow> rows, bool filesShared,
    const std::vector<TransferRecord>& transfers) {
    if (!filesShared) return rows;

    HostRow files;
    files.sourceId = kFilesSourceId;
    files.files = true;
    rows.push_back(files);
    for (const TransferRecord& transfer : transfers) {
        HostRow row;
        row.viewer = true;
        row.sourceId = kFilesSourceId;
        row.files = true;
        row.peerPacked = transfer.peerPacked;
        row.viewerAddr = transfer.endpoint;
        row.viewerName = transfer.peerName;
        rows.push_back(row);
    }
    return rows;
}

}

std::string ViewerLabel(const std::string& name, const std::string& addr) {
    if (name.empty()) return addr;
    return name + " (" + addr + ")";
}

std::vector<HostRow> BuildHostRows(const std::vector<media::AgentSourceStatus>& sources) {
    return BuildHostRows(sources, false, {});
}

std::vector<HostRow> BuildHostRows(const std::vector<media::AgentSourceStatus>& sources,
    bool terminalShared, const std::vector<TerminalRecord>& shells) {
    return BuildHostRows(sources, terminalShared, shells, false, {});
}

std::vector<HostRow> BuildHostRows(const std::vector<media::AgentSourceStatus>& sources,
    bool terminalShared, const std::vector<TerminalRecord>& shells, bool filesShared,
    const std::vector<TransferRecord>& transfers) {
    std::vector<HostRow> rows;
    for (const media::AgentSourceStatus& source : sources) {
        rows.push_back(HostRow{false, source.sourceId, {}, {}});
        for (size_t i = 0; i < source.viewerAddrs.size(); ++i) {
            const std::string name =
                i < source.viewerNames.size() ? source.viewerNames[i] : std::string();
            rows.push_back(HostRow{true, source.sourceId, source.viewerAddrs[i], name});
        }
    }
    if (!terminalShared) return AppendFileRows(std::move(rows), filesShared, transfers);

    HostRow terminal;
    terminal.sourceId = kTerminalSourceId;
    terminal.terminal = true;
    rows.push_back(terminal);
    for (const TerminalRecord& shell : shells) {
        HostRow row;
        row.viewer = true;
        row.sourceId = kTerminalSourceId;
        row.viewerAddr = shell.clientEndpoint;
        row.viewerName = shell.clientName;
        row.terminal = true;
        row.termId = shell.termId;
        row.shellState = shell.state;
        rows.push_back(row);
    }
    return AppendFileRows(std::move(rows), filesShared, transfers);
}

const media::AgentSourceStatus* FindHostSource(
    const std::vector<media::AgentSourceStatus>& sources, uint8_t sourceId) {
    for (const media::AgentSourceStatus& source : sources)
        if (source.sourceId == sourceId) return &source;
    return nullptr;
}

const TerminalRecord* FindShell(const std::vector<TerminalRecord>& shells, uint32_t termId) {
    for (const TerminalRecord& shell : shells)
        if (shell.termId == termId) return &shell;
    return nullptr;
}

HostRowCells TerminalRowText(const HostRow& row, uint16_t port,
    const std::vector<TerminalRecord>& shells) {
    HostRowCells cells;
    if (row.viewer) {
        const TerminalRecord* shell = FindShell(shells, row.termId);
        cells.source = kShellRowLabel;
        if (shell != nullptr && shell->state == TerminalState::Local) {
            cells.client = kTerminalLocalClient;
        } else {
            cells.client = ViewerLabel(row.viewerName, row.viewerAddr);
            if (shell != nullptr && shell->state == TerminalState::Detached)
                cells.client += std::string(" ") + kTerminalDetached;
        }
        if (shell != nullptr) cells.size = media::SourceSizeLabel(shell->size.cols,
                                  shell->size.rows);
        cells.online = shell != nullptr && shell->state != TerminalState::Detached;
        return cells;
    }

    size_t live = 0;
    for (const TerminalRecord& shell : shells)
        if (shell.state != TerminalState::Detached) ++live;

    cells.source = kTerminalSourceName;
    cells.size = PortCell(port);
    cells.viewers = std::to_string(shells.size());
    cells.rtt = "-";
    cells.online = live > 0;
    return cells;
}

const TransferRecord* FindTransfer(const std::vector<TransferRecord>& transfers,
    uint64_t peerPacked) {
    for (const TransferRecord& transfer : transfers)
        if (transfer.peerPacked == peerPacked) return &transfer;
    return nullptr;
}

HostRowCells FilesRowText(const HostRow& row, std::string_view folder,
    const std::vector<TransferRecord>& transfers) {
    HostRowCells cells;
    if (row.viewer) {
        const TransferRecord* transfer = FindTransfer(transfers, row.peerPacked);
        cells.source = kSendingRowLabel;
        cells.client = ViewerLabel(row.viewerName, row.viewerAddr);
        if (transfer == nullptr) return cells;
        cells.size = transfer->name;
        cells.viewers = transfer->fileCount
                            ? std::to_string(transfer->fileIndex + 1) + "/" +
                                  std::to_string(transfer->fileCount)
                            : std::string();
        cells.send = transfer->live ? TransferPercent(*transfer)
                                    : std::string(TransferReasonName(transfer->reason));
        cells.online = transfer->live;
        return cells;
    }

    size_t live = 0;
    for (const TransferRecord& transfer : transfers)
        if (transfer.live) ++live;

    cells.source = kFilesSourceName;
    cells.client = std::string(folder);
    cells.viewers = std::to_string(live);
    cells.rtt = "-";
    cells.online = live > 0;
    return cells;
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
