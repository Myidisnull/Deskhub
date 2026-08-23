#pragma once
#include "deskhub/media/ShareTypes.h"
#include "deskhub/session/FileTransfer.h"
#include "deskhub/session/TerminalSession.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace deskhub::ui {

inline constexpr const char* kViewerRowLabel = "    \xE2\x86\xB3 viewer";
inline constexpr const char* kShellRowLabel = "    \xE2\x86\xB3 shell";
inline constexpr const char* kSendingRowLabel = "    \xE2\x86\xB3 sending";
inline constexpr uint8_t kTerminalSourceId = 0xFF;
inline constexpr uint8_t kFilesSourceId = 0xFE;

struct HostRow {
    bool viewer = false;
    uint8_t sourceId = 0;
    std::string viewerAddr{};
    std::string viewerName{};
    bool terminal = false;
    uint32_t termId = 0;
    TerminalState shellState = TerminalState::Live;
    bool files = false;
    uint64_t peerPacked = 0;

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

std::vector<HostRow> BuildHostRows(const std::vector<media::ShareSourceStatus>& sources);

std::vector<HostRow> BuildHostRows(const std::vector<media::ShareSourceStatus>& sources,
    bool terminalShared, const std::vector<TerminalRecord>& shells);

std::vector<HostRow> BuildHostRows(const std::vector<media::ShareSourceStatus>& sources,
    bool terminalShared, const std::vector<TerminalRecord>& shells, bool filesShared,
    const std::vector<TransferRecord>& transfers);

const media::ShareSourceStatus* FindHostSource(
    const std::vector<media::ShareSourceStatus>& sources, uint8_t sourceId);

const TerminalRecord* FindShell(const std::vector<TerminalRecord>& shells, uint32_t termId);

HostRowCells HostRowText(const HostRow& row, const media::ShareSourceStatus& source);

HostRowCells TerminalRowText(const HostRow& row, uint16_t port,
    const std::vector<TerminalRecord>& shells);

const TransferRecord* FindTransfer(const std::vector<TransferRecord>& transfers,
    uint64_t peerPacked);

HostRowCells FilesRowText(const HostRow& row, std::string_view folder,
    const std::vector<TransferRecord>& transfers);

}
