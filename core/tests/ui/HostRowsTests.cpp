#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/HostRows.h"
#include "deskhub/ui/Strings.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

media::ShareSourceStatus MakeSource(uint8_t id, std::vector<std::string> viewers,
    std::vector<std::string> names = {}) {
    media::ShareSourceStatus s;
    s.sourceId = id;
    s.name = "Display " + std::to_string(id);
    s.width = 1920;
    s.height = 1080;
    s.viewerCount = uint32_t(viewers.size());
    s.viewerConnected = !viewers.empty();
    s.viewerAddr = viewers.empty() ? std::string() : viewers.front();
    s.viewerAddrs = std::move(viewers);
    s.viewerNames = std::move(names);
    s.captureFps = 59.6;
    s.sendFps = 58.4;
    s.sendKbps = 12345.0;
    s.rttMs = 7;
    return s;
}

void TestEverySourceGetsARowFollowedByItsViewers() {
    std::printf("[hostrows] each shared display lists its viewers underneath it...\n");
    const std::vector<media::ShareSourceStatus> sources{
        MakeSource(1, {"192.168.1.7:47777", "192.168.1.9:47777"}),
        MakeSource(2, {}),
    };
    const std::vector<ui::HostRow> rows = ui::BuildHostRows(sources);

    Check(rows.size() == 4, "two displays with two viewers make four rows");
    Check(!rows[0].viewer && rows[0].sourceId == 1, "the display comes first");
    Check(rows[1].viewer && rows[1].viewerAddr == "192.168.1.7:47777",
        "its first viewer follows it");
    Check(rows[2].viewer && rows[2].viewerAddr == "192.168.1.9:47777",
        "then its second viewer");
    Check(!rows[3].viewer && rows[3].sourceId == 2, "a display with no viewer stands alone");
}

void TestRowsCompareEqualWhileNothingChanges() {
    std::printf("[hostrows] an unchanged host rebuilds the same row list...\n");
    const std::vector<media::ShareSourceStatus> sources{MakeSource(3, {"10.0.0.4:47777"})};
    Check(ui::BuildHostRows(sources) == ui::BuildHostRows(sources),
        "polling twice with the same status yields identical rows");

    const std::vector<media::ShareSourceStatus> joined{MakeSource(3, {"10.0.0.5:47777"})};
    Check(!(ui::BuildHostRows(sources) == ui::BuildHostRows(joined)),
        "a different viewer address is a different row list");
}

void TestSourceCellsReadLikeTheTable() {
    std::printf("[hostrows] a display row spells out size, rate and ping...\n");
    const media::ShareSourceStatus source = MakeSource(1, {"192.168.1.7:47777"});
    const ui::HostRowCells cells = ui::HostRowText(ui::HostRow{false, 1, {}}, source);

    Check(cells.source == "Display 1", "the first column names the display");
    Check(cells.size == "1920x1080", "the size column is width x height");
    Check(cells.viewers == "1", "the viewer count is a plain number");
    Check(cells.client.empty(), "the client column belongs to the viewer rows");
    Check(cells.capture == "60" && cells.send == "58", "frame rates are whole numbers");
    Check(cells.mbps == "12.3", "the send rate is megabits with one decimal");
    Check(cells.rtt == ui::PingMs(7), "the ping matches the shared ping text");
    Check(cells.online, "a display with a viewer reads as online");
}

void TestIdleSourceHasNoPing() {
    std::printf("[hostrows] a display nobody watches shows a dash instead of a ping...\n");
    const media::ShareSourceStatus source = MakeSource(1, {});
    const ui::HostRowCells cells = ui::HostRowText(ui::HostRow{false, 1, {}}, source);

    Check(cells.rtt == "-", "no viewer means no round trip to report");
    Check(cells.viewers == "0", "and no viewers counted");
    Check(!cells.online, "an idle display is not tinted as online");
}

void TestViewerRowOnlyNamesTheClient() {
    std::printf("[hostrows] a viewer row is indented and carries just the address...\n");
    const media::ShareSourceStatus source = MakeSource(1, {"192.168.1.7:47777"});
    const ui::HostRowCells cells =
        ui::HostRowText(ui::HostRow{true, 1, "192.168.1.7:47777"}, source);

    Check(cells.source == ui::kViewerRowLabel, "the first column marks it as a viewer");
    Check(cells.client == "192.168.1.7:47777", "the client column holds the viewer address");
    Check(cells.size.empty() && cells.viewers.empty() && cells.capture.empty() &&
              cells.send.empty() && cells.mbps.empty() && cells.rtt.empty(),
        "the display's own numbers are not repeated on the viewer row");
    Check(cells.online, "a connected viewer reads as online");
}

void TestViewerRowShowsTheClientName() {
    std::printf("[hostrows] a named viewer shows its name in front of the address...\n");
    const std::vector<media::ShareSourceStatus> sources{
        MakeSource(1, {"192.168.1.7:47777", "192.168.1.9:47777"}, {"Anh's laptop", ""}),
    };
    const std::vector<ui::HostRow> rows = ui::BuildHostRows(sources);

    Check(rows.size() == 3, "one display and two viewers make three rows");
    Check(rows[1].viewerName == "Anh's laptop", "the first viewer carries its name");
    Check(rows[2].viewerName.empty(), "the second viewer has none");

    const ui::HostRowCells named = ui::HostRowText(rows[1], sources[0]);
    Check(named.client == "Anh's laptop (192.168.1.7:47777)",
        "the client cell reads name then address");
    const ui::HostRowCells unnamed = ui::HostRowText(rows[2], sources[0]);
    Check(unnamed.client == "192.168.1.9:47777",
        "an unnamed viewer falls back to the bare address");
    Check(rows[1].viewerAddr == "192.168.1.7:47777",
        "the row still keys on the address for kicks");

    const std::vector<media::ShareSourceStatus> fewerNames{
        MakeSource(1, {"192.168.1.7:47777", "192.168.1.9:47777"}, {"Anh's laptop"}),
    };
    const std::vector<ui::HostRow> partial = ui::BuildHostRows(fewerNames);
    Check(partial.size() == 3 && partial[2].viewerName.empty(),
        "a short name list never misaligns the rows");

    const std::vector<media::ShareSourceStatus> renamed{
        MakeSource(1, {"192.168.1.7:47777"}, {"Bob's phone"}),
    };
    Check(!(ui::BuildHostRows(sources) == ui::BuildHostRows(renamed)),
        "a name change is a different row list");
}

TerminalRecord MakeShell(uint32_t termId, std::string endpoint, std::string name,
    TerminalState state = TerminalState::Live) {
    TerminalRecord record;
    record.termId = termId;
    record.state = state;
    record.size = TermSize{80, 24};
    record.clientEndpoint = std::move(endpoint);
    record.clientName = std::move(name);
    return record;
}

void TestTerminalIsARowInTheSameTable() {
    std::printf("[hostrows] a shared terminal sits in the table beside the displays...\n");
    const std::vector<media::ShareSourceStatus> sources{MakeSource(1, {"192.168.1.7:47777"})};
    const std::vector<TerminalRecord> shells{
        MakeShell(4, "192.168.1.9:51000", "Anh's laptop"),
        MakeShell(5, "192.168.1.9:51001", "", TerminalState::Detached),
    };
    const std::vector<ui::HostRow> rows = ui::BuildHostRows(sources, true, shells);

    Check(rows.size() == 5, "one display, its viewer, the terminal and its two shells");
    Check(!rows[2].viewer && rows[2].terminal && rows[2].sourceId == ui::kTerminalSourceId,
        "the terminal follows the displays as a source of its own");
    Check(rows[3].viewer && rows[3].terminal && rows[3].termId == 4,
        "each open shell hangs off the terminal row");
    Check(rows[4].termId == 5, "in the order the host reports them");
    Check(!rows[0].terminal && !rows[1].terminal, "display rows are not marked as terminal");

    Check(ui::BuildHostRows(sources, false, shells) == ui::BuildHostRows(sources),
        "no terminal row while the terminal is not shared");
    Check(ui::BuildHostRows(sources, true, {}).size() == 3,
        "a shared terminal with nobody attached is still one row");
}

void TestTerminalCellsReadLikeTheTable() {
    std::printf("[hostrows] the terminal row shows its port and how many shells are open...\n");
    const std::vector<TerminalRecord> shells{
        MakeShell(4, "192.168.1.9:51000", "Anh's laptop"),
        MakeShell(5, "192.168.1.9:51001", "", TerminalState::Detached),
    };
    const std::vector<ui::HostRow> rows = ui::BuildHostRows({}, true, shells);

    const ui::HostRowCells terminal = ui::TerminalRowText(rows[0], 47778, shells);
    Check(terminal.source == ui::kTerminalSourceName, "the first column names the terminal");
    Check(terminal.size == ui::PortCell(47778), "the size column carries the terminal port");
    Check(terminal.viewers == "2", "both shells are counted, attached or not");
    Check(terminal.rtt == "-" && terminal.capture.empty(),
        "a shell has no frame rate or ping to report");
    Check(terminal.online, "one live shell tints the terminal as online");

    const ui::HostRowCells live = ui::TerminalRowText(rows[1], 47778, shells);
    Check(live.source == ui::kShellRowLabel, "a shell row is indented like a viewer row");
    Check(live.client == "Anh's laptop (192.168.1.9:51000)",
        "the client cell reads name then address");
    Check(live.size == "80x24", "the shell row carries the grid it is running at");
    Check(live.online, "an attached shell reads as online");

    const ui::HostRowCells detached = ui::TerminalRowText(rows[2], 47778, shells);
    Check(detached.client == "192.168.1.9:51001 " + std::string(ui::kTerminalDetached),
        "a dropped client is marked as detached");
    Check(!detached.online, "and is not tinted as online");

    const std::vector<TerminalRecord> gone{};
    const ui::HostRowCells vanished = ui::TerminalRowText(rows[1], 47778, gone);
    Check(vanished.client == "Anh's laptop (192.168.1.9:51000)" && !vanished.online,
        "a shell that ended between polls still renders, greyed out");
    Check(ui::TerminalRowText(rows[0], 47778, gone).viewers == "0",
        "and the terminal row counts none");
}

void TestLocallyAttachedShellReadsAsTheHostsOwn() {
    std::printf("[hostrows] a shell the host took over says so instead of naming a client...\n");
    const std::vector<TerminalRecord> shells{
        MakeShell(4, "192.168.1.9:51000", "Anh's laptop", TerminalState::Local),
    };
    const std::vector<ui::HostRow> rows = ui::BuildHostRows({}, true, shells);

    Check(rows.size() == 2 && rows[1].shellState == TerminalState::Local,
        "the row carries the state, so the table rebuilds when it changes");
    const ui::HostRowCells cells = ui::TerminalRowText(rows[1], 47778, shells);
    Check(cells.client == ui::kTerminalLocalClient,
        "the client cell says the shell lives on this machine now");
    Check(cells.online, "and it reads as online");
    Check(ui::TerminalRowText(rows[0], 47778, shells).online,
        "a local shell keeps the terminal row tinted as online");

    const std::vector<TerminalRecord> before{MakeShell(4, "192.168.1.9:51000", "Anh's laptop")};
    Check(!(ui::BuildHostRows({}, true, shells) == ui::BuildHostRows({}, true, before)),
        "taking a shell over is a different row list");
}

void TestShellLookupByTermId() {
    std::printf("[hostrows] a shell row finds its session by id...\n");
    const std::vector<TerminalRecord> shells{MakeShell(7, "10.0.0.4:51000", "")};
    const TerminalRecord* found = ui::FindShell(shells, 7);
    Check(found != nullptr && found->termId == 7, "the session is found by id");
    Check(ui::FindShell(shells, 8) == nullptr, "an unknown id finds nothing");
}

void TestSourceLookupByIdIgnoresOrder() {
    std::printf("[hostrows] a row finds its display whatever order the host reports...\n");
    const std::vector<media::ShareSourceStatus> sources{MakeSource(5, {}), MakeSource(2, {})};

    const media::ShareSourceStatus* found = ui::FindHostSource(sources, 2);
    Check(found && found->sourceId == 2, "the second display is found by id");
    Check(ui::FindHostSource(sources, 9) == nullptr, "an unknown id finds nothing");
}

}

TransferRecord MakeTransfer(uint64_t peer, std::string endpoint, std::string name,
    uint16_t index, uint16_t count, uint64_t done, uint64_t total, bool live) {
    TransferRecord record;
    record.peerPacked = peer;
    record.endpoint = std::move(endpoint);
    record.peerName = std::move(name);
    record.batchId = 1;
    record.fileIndex = index;
    record.fileCount = count;
    record.batchBytes = done;
    record.batchSize = total;
    record.name = "payload.bin";
    record.live = live;
    return record;
}

void TestFileTransferIsARowInTheSameTable() {
    std::printf("[hostrows] file transfer sits in the table like any other source...\n");
    const std::vector<media::ShareSourceStatus> sources{MakeSource(1, {"192.168.1.7:47777"})};
    const std::vector<TransferRecord> transfers{
        MakeTransfer(0xAB01, "192.168.1.9:51000", "Anh's laptop", 0, 3, 50, 200, true),
        MakeTransfer(0xAB02, "192.168.1.4:51001", "", 2, 3, 200, 200, false),
    };
    const std::vector<ui::HostRow> rows = ui::BuildHostRows(sources, false, {}, true, transfers);

    Check(rows.size() == 5, "one display, its viewer, the file source and its two senders");
    Check(!rows[2].viewer && rows[2].files && rows[2].sourceId == ui::kFilesSourceId,
        "file transfer follows the displays as a source of its own");
    Check(rows[3].viewer && rows[3].files && rows[3].peerPacked == 0xAB01,
        "each machine sending hangs off that row");
    Check(!rows[0].files && !rows[1].files, "display rows are not marked as files");

    Check(ui::BuildHostRows(sources, false, {}, false, transfers) == ui::BuildHostRows(sources),
        "no file row while file transfer is not shared");
    Check(ui::BuildHostRows(sources, false, {}, true, {}).size() == 3,
        "sharing it with nobody sending is still one row");

    const std::vector<TerminalRecord> shells{MakeShell(4, "192.168.1.9:51000", "laptop")};
    const std::vector<ui::HostRow> both =
        ui::BuildHostRows(sources, true, shells, true, transfers);
    Check(both.size() == 7, "the terminal and the file source coexist");
    Check(both[2].terminal && both[4].files, "the terminal comes first, then file transfer");
}

void TestFileRowCellsReadLikeTheTable() {
    std::printf("[hostrows] the file row names its folder and who is sending...\n");
    const std::vector<TransferRecord> transfers{
        MakeTransfer(0xAB01, "192.168.1.9:51000", "Anh's laptop", 0, 3, 50, 200, true),
        MakeTransfer(0xAB02, "192.168.1.4:51001", "", 2, 3, 200, 200, false),
    };
    const std::vector<ui::HostRow> rows = ui::BuildHostRows({}, false, {}, true, transfers);

    const ui::HostRowCells source = ui::FilesRowText(rows[0], "/Users/anh/Deskhub", transfers);
    Check(source.source == ui::kFilesSourceName, "the source cell names the feature");
    Check(source.client == "/Users/anh/Deskhub", "the folder is where the viewers land");
    Check(source.viewers == "1", "only the machines actually sending are counted");
    Check(source.online, "and the row reads as busy while one is");

    const ui::HostRowCells busy = ui::FilesRowText(rows[1], "/Users/anh/Deskhub", transfers);
    Check(busy.source == ui::kSendingRowLabel, "a sender is an indented row");
    Check(busy.client == "Anh's laptop (192.168.1.9:51000)", "named the way viewers are");
    Check(busy.size == "payload.bin", "the file on its way is shown");
    Check(busy.viewers == "1/3", "with how far through the batch it is");
    Check(busy.send == "25%", "and how much of the batch has landed");
    Check(busy.online, "a live sender reads as online");

    const ui::HostRowCells done = ui::FilesRowText(rows[2], "/Users/anh/Deskhub", transfers);
    Check(done.client == "192.168.1.4:51001", "a machine with no name shows its address");
    Check(done.send == "accepted", "a finished batch says how it ended, not a percentage");
    Check(!done.online, "and no longer reads as online");

    const std::vector<ui::HostRow> quiet = ui::BuildHostRows({}, false, {}, true, {});
    const ui::HostRowCells idle = ui::FilesRowText(quiet[0], "/Users/anh/Deskhub", {});
    Check(idle.viewers == "0" && !idle.online, "nobody sending reads as quiet");
}

void TestTransferLookupByPeer() {
    std::printf("[hostrows] a sender row finds its own transfer, whatever the order...\n");
    const std::vector<TransferRecord> transfers{
        MakeTransfer(0xAB01, "a", "", 0, 1, 1, 2, true),
        MakeTransfer(0xAB02, "b", "", 0, 1, 1, 2, true),
    };
    Check(ui::FindTransfer(transfers, 0xAB02) == &transfers[1], "the second is found by its peer");
    Check(ui::FindTransfer(transfers, 0xFFFF) == nullptr, "and an unknown peer finds nothing");
}

void RunHostRowsTests() {
    TestEverySourceGetsARowFollowedByItsViewers();
    TestRowsCompareEqualWhileNothingChanges();
    TestSourceCellsReadLikeTheTable();
    TestIdleSourceHasNoPing();
    TestViewerRowOnlyNamesTheClient();
    TestViewerRowShowsTheClientName();
    TestTerminalIsARowInTheSameTable();
    TestTerminalCellsReadLikeTheTable();
    TestLocallyAttachedShellReadsAsTheHostsOwn();
    TestShellLookupByTermId();
    TestSourceLookupByIdIgnoresOrder();
    TestFileTransferIsARowInTheSameTable();
    TestFileRowCellsReadLikeTheTable();
    TestTransferLookupByPeer();
}
