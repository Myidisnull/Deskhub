#include "Perf.h"
#include "PerfHarness.h"

#include "deskhub/terminal/Repaint.h"
#include "deskhub/terminal/Screen.h"
#include "deskhub/terminal/Snapshot.h"
#include "deskhub/terminal/VtParser.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

using namespace deskhub;
using namespace deskhub::perf;

namespace {

constexpr size_t kLogBytes = 256 * 1024;
constexpr size_t kBytesPerKilobyte = 1024;
constexpr uint16_t kNarrowCols = 80;
constexpr uint16_t kNarrowRows = 24;
constexpr uint16_t kWideCols = 200;
constexpr uint16_t kWideRows = 50;
constexpr size_t kDeepScrollback = 50'000;

std::string BuildLog(size_t bytes, bool coloured) {
    std::string log;
    log.reserve(bytes + 256);
    char line[256] = {};
    while (log.size() < bytes) {
        const int percent = int(NextRandom() % 100);
        const unsigned object = NextRandom() % 9999;
        if (coloured)
            std::snprintf(line, sizeof(line),
                "\x1b[1;32m[%3d%%]\x1b[0m \x1b[36mBuilding\x1b[0m CXX object core/CMakeFiles/core.dir/src/session/Unit%04u.cpp.o\n",
                percent, object);
        else
            std::snprintf(line, sizeof(line),
                "[%3d%%] Building CXX object core/CMakeFiles/core.dir/src/session/Unit%04u.cpp.o\n",
                percent, object);
        log.append(line);
    }
    return log;
}

std::span<const uint8_t> BytesOf(const std::string& text) {
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

}

void RunTerminalPerf() {
    BeginGroup("terminal");

    const std::string plainLog = BuildLog(kLogBytes, false);
    const std::string colouredLog = BuildLog(kLogBytes, true);

    term::VtParser parser;
    std::vector<term::VtEvent> events;
    Measure(Workload{"terminal/vt-parse-plain", "KB", plainLog.size() / kBytesPerKilobyte,
        plainLog.size(), 0.2, [&] {
            events.clear();
            parser.Parse(BytesOf(plainLog), events);
            Consume(events.size());
        }});

    term::VtParser colourParser;
    std::vector<term::VtEvent> colourEvents;
    Measure(Workload{"terminal/vt-parse-coloured", "KB", colouredLog.size() / kBytesPerKilobyte,
        colouredLog.size(), 0.2, [&] {
            colourEvents.clear();
            colourParser.Parse(BytesOf(colouredLog), colourEvents);
            Consume(colourEvents.size());
        }});

    term::Screen narrow(TermSize{kNarrowCols, kNarrowRows});
    Measure(Workload{"terminal/screen-write-80x24", "KB", plainLog.size() / kBytesPerKilobyte,
        plainLog.size(), 32.0, [&] {
            narrow.Write(plainLog);
            Consume(narrow.Revision());
        }});

    term::Screen wide(TermSize{kWideCols, kWideRows});
    Measure(Workload{"terminal/screen-write-200x50", "KB", colouredLog.size() / kBytesPerKilobyte,
        colouredLog.size(), 32.0, [&] {
            wide.Write(colouredLog);
            Consume(wide.Revision());
        }});

    Measure(Workload{"terminal/snapshot-200x50", "snapshot", 1, 0, 8.0, [&] {
        const term::TerminalSnapshot snapshot = term::SnapshotScreen(wide, 0);
        Consume(snapshot.cells.size());
    }});

    Measure(Workload{"terminal/render-200x50", "render", 1, 0, 12.0, [&] {
        Consume(term::RenderScreen(wide).size());
    }});

    term::Screen scalingScreen(TermSize{kNarrowCols, kNarrowRows});
    MeasureScaling(ScalingWorkload{"terminal/screen-write-scaling", "KB", 64, 256, 7.0,
        [&](uint64_t units) {
            const size_t bytes = size_t(units) * kBytesPerKilobyte;
            scalingScreen.Write(std::string_view(plainLog.data(), bytes));
            Consume(scalingScreen.Revision());
        }});

    term::Screen deepScreen(TermSize{kNarrowCols, kNarrowRows}, kDeepScrollback);
    MeasureScaling(ScalingWorkload{"terminal/scrollback-scaling", "KB", 64, 256, 7.0,
        [&](uint64_t units) {
            const size_t bytes = size_t(units) * kBytesPerKilobyte;
            deepScreen.Write(std::string_view(plainLog.data(), bytes));
            Consume(deepScreen.ScrollbackRows());
        }});
}
