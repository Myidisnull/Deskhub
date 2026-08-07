#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/HostRows.h"
#include "deskhub/ui/Strings.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

media::AgentSourceStatus MakeSource(uint8_t id, std::vector<std::string> viewers) {
    media::AgentSourceStatus s;
    s.sourceId = id;
    s.name = "Display " + std::to_string(id);
    s.width = 1920;
    s.height = 1080;
    s.viewerCount = uint32_t(viewers.size());
    s.viewerConnected = !viewers.empty();
    s.viewerAddr = viewers.empty() ? std::string() : viewers.front();
    s.viewerAddrs = std::move(viewers);
    s.captureFps = 59.6;
    s.sendFps = 58.4;
    s.sendKbps = 12345.0;
    s.rttMs = 7;
    return s;
}

void TestEverySourceGetsARowFollowedByItsViewers() {
    std::printf("[hostrows] each shared display lists its viewers underneath it...\n");
    const std::vector<media::AgentSourceStatus> sources{
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
    const std::vector<media::AgentSourceStatus> sources{MakeSource(3, {"10.0.0.4:47777"})};
    Check(ui::BuildHostRows(sources) == ui::BuildHostRows(sources),
        "polling twice with the same status yields identical rows");

    const std::vector<media::AgentSourceStatus> joined{MakeSource(3, {"10.0.0.5:47777"})};
    Check(!(ui::BuildHostRows(sources) == ui::BuildHostRows(joined)),
        "a different viewer address is a different row list");
}

void TestSourceCellsReadLikeTheTable() {
    std::printf("[hostrows] a display row spells out size, rate and ping...\n");
    const media::AgentSourceStatus source = MakeSource(1, {"192.168.1.7:47777"});
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
    const media::AgentSourceStatus source = MakeSource(1, {});
    const ui::HostRowCells cells = ui::HostRowText(ui::HostRow{false, 1, {}}, source);

    Check(cells.rtt == "-", "no viewer means no round trip to report");
    Check(cells.viewers == "0", "and no viewers counted");
    Check(!cells.online, "an idle display is not tinted as online");
}

void TestViewerRowOnlyNamesTheClient() {
    std::printf("[hostrows] a viewer row is indented and carries just the address...\n");
    const media::AgentSourceStatus source = MakeSource(1, {"192.168.1.7:47777"});
    const ui::HostRowCells cells =
        ui::HostRowText(ui::HostRow{true, 1, "192.168.1.7:47777"}, source);

    Check(cells.source == ui::kViewerRowLabel, "the first column marks it as a viewer");
    Check(cells.client == "192.168.1.7:47777", "the client column holds the viewer address");
    Check(cells.size.empty() && cells.viewers.empty() && cells.capture.empty() &&
              cells.send.empty() && cells.mbps.empty() && cells.rtt.empty(),
        "the display's own numbers are not repeated on the viewer row");
    Check(cells.online, "a connected viewer reads as online");
}

void TestSourceLookupByIdIgnoresOrder() {
    std::printf("[hostrows] a row finds its display whatever order the host reports...\n");
    const std::vector<media::AgentSourceStatus> sources{MakeSource(5, {}), MakeSource(2, {})};

    const media::AgentSourceStatus* found = ui::FindHostSource(sources, 2);
    Check(found && found->sourceId == 2, "the second display is found by id");
    Check(ui::FindHostSource(sources, 9) == nullptr, "an unknown id finds nothing");
}

}

void RunHostRowsTests() {
    TestEverySourceGetsARowFollowedByItsViewers();
    TestRowsCompareEqualWhileNothingChanges();
    TestSourceCellsReadLikeTheTable();
    TestIdleSourceHasNoPing();
    TestViewerRowOnlyNamesTheClient();
    TestSourceLookupByIdIgnoresOrder();
}
