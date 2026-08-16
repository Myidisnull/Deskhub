#include "deskhub/terminal/Snapshot.h"

#include <algorithm>

namespace deskhub::term {

namespace {

const Cell kBlankCell{};

}

const Cell& TerminalSnapshot::At(uint16_t row, uint16_t col) const {
    if (row >= size.rows || col >= size.cols) return kBlankCell;
    const size_t at = size_t(row) * size.cols + col;
    if (at >= cells.size()) return kBlankCell;
    return cells[at];
}

TerminalSnapshot SnapshotScreen(const Screen& screen, size_t scrollOffset) {
    TerminalSnapshot out;
    out.size = screen.Size();
    out.cursor = screen.Cursor();
    out.title = screen.Title();
    out.revision = screen.Revision();
    out.scrollbackRows = screen.AlternateScreen() ? 0 : screen.ScrollbackRows();
    out.scrollOffset = std::min(scrollOffset, out.scrollbackRows);

    const size_t history = out.scrollbackRows;
    const size_t first = history - out.scrollOffset;
    out.cells.reserve(size_t(out.size.rows) * out.size.cols);
    for (uint16_t r = 0; r < out.size.rows; ++r) {
        const size_t line = first + r;
        for (uint16_t c = 0; c < out.size.cols; ++c) {
            out.cells.push_back(line < history ? screen.ScrollbackAt(line, c)
                                               : screen.At(uint16_t(line - history), c));
        }
    }
    if (out.scrollOffset != 0) out.cursor.visible = false;
    return out;
}

}
