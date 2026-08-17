#pragma once
#include "deskhub/terminal/KeyEncoder.h"
#include "deskhub/terminal/Palette.h"
#include "deskhub/terminal/Snapshot.h"
#include "deskhubp/ffi/TerminalFfi.h"

#include <cstdint>

namespace deskhubp {

inline bool FillTermGrid(const deskhub::term::TerminalSnapshot& shot, DHTermCell* cells,
    uint32_t cellCapacity, DHTermGrid* outGrid) {
    if (outGrid == nullptr) return false;

    outGrid->rows = shot.size.rows;
    outGrid->cols = shot.size.cols;
    outGrid->cursorRow = shot.cursor.row;
    outGrid->cursorCol = shot.cursor.col;
    outGrid->cursorVisible = shot.cursor.visible;
    outGrid->scrollbackRows = uint32_t(shot.scrollbackRows);
    outGrid->scrollOffset = uint32_t(shot.scrollOffset);
    outGrid->revision = shot.revision;

    const uint32_t needed = uint32_t(shot.size.rows) * shot.size.cols;
    if (cells == nullptr || cellCapacity < needed) return false;

    for (uint32_t i = 0; i < needed && i < shot.cells.size(); ++i) {
        const deskhub::term::Cell& cell = shot.cells[i];
        const deskhub::term::CellColors colors = deskhub::term::ResolveCell(cell.pen, false);
        DHTermCell& out = cells[i];
        out.codepoint = uint32_t(cell.ch);
        out.fgR = colors.fg.r;
        out.fgG = colors.fg.g;
        out.fgB = colors.fg.b;
        out.bgR = colors.bg.r;
        out.bgG = colors.bg.g;
        out.bgB = colors.bg.b;
        out.attrs = cell.pen.attrs;
    }
    return true;
}

inline bool DecodeTermKey(int32_t key, uint32_t codepoint, bool shift, bool alt, bool ctrl,
    deskhub::term::TermKeyEvent& out) {
    if (key < DHTermKeyChar || key > DHTermKeyF12) return false;
    out.key = deskhub::term::TermKey(key);
    out.codepoint = char32_t(codepoint);
    out.mods.shift = shift;
    out.mods.alt = alt;
    out.mods.ctrl = ctrl;
    return true;
}

}
