#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/terminal/Snapshot.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

std::string RowOf(const term::TerminalSnapshot& shot, uint16_t row) {
    std::string line;
    for (uint16_t c = 0; c < shot.size.cols; ++c) line += term::EncodeUtf8(shot.At(row, c).ch);
    return line;
}

void TestSnapshotCopiesTheLiveGrid() {
    std::printf("[snapshot] a snapshot is the grid, one cell per position...\n");
    term::Screen screen(TermSize{10, 3});
    screen.Write("hi");

    const term::TerminalSnapshot shot = term::SnapshotScreen(screen, 0);
    Check(shot.size == TermSize{10, 3}, "the snapshot is the screen's size");
    Check(shot.cells.size() == size_t(shot.size.rows) * shot.size.cols,
        "with one cell per position");
    Check(RowOf(shot, 0).rfind("hi", 0) == 0, "holding what the screen holds");
    Check(shot.cursor == screen.Cursor(), "and the cursor where the screen left it");
    Check(shot.scrollOffset == 0 && shot.cursor.visible,
        "a live view sits at the bottom with the cursor shown");
    Check(shot.At(999, 999) == term::Cell{}, "reading outside it is blank, not a crash");
}

void TestSnapshotWalksTheScrollback() {
    std::printf("[snapshot] a scrolled snapshot shows history and hides the cursor...\n");
    term::Screen screen(TermSize{10, 2});
    screen.Write("first\r\nsecond\r\nthird\r\nfourth\r\n");
    Check(screen.ScrollbackRows() > 0, "enough lines pushed some into the scrollback");

    const term::TerminalSnapshot back = term::SnapshotScreen(screen, screen.ScrollbackRows());
    Check(back.scrollOffset == screen.ScrollbackRows(), "the view walked back to the top");
    Check(RowOf(back, 0).rfind("first", 0) == 0, "where the first line still reads");
    Check(!back.cursor.visible, "with no cursor drawn, because it is not on this screen");

    const term::TerminalSnapshot clamped = term::SnapshotScreen(screen, 999999);
    Check(clamped.scrollOffset == clamped.scrollbackRows,
        "scrolling past the oldest line stops there");

    screen.Write("\x1b[?1049h");
    const term::TerminalSnapshot alternate = term::SnapshotScreen(screen, 5);
    Check(alternate.scrollbackRows == 0 && alternate.scrollOffset == 0,
        "the alternate screen offers no history to walk into");
}

}

void RunSnapshotTests() {
    TestSnapshotCopiesTheLiveGrid();
    TestSnapshotWalksTheScrollback();
}
