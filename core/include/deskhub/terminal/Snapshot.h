#pragma once
#include "deskhub/terminal/Screen.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deskhub::term {

struct TerminalSnapshot {
    TermSize size{};
    std::vector<Cell> cells{};
    CursorState cursor{};
    std::string title{};
    uint64_t revision = 0;
    size_t scrollbackRows = 0;
    size_t scrollOffset = 0;

    const Cell& At(uint16_t row, uint16_t col) const;
};

TerminalSnapshot SnapshotScreen(const Screen& screen, size_t scrollOffset);

}
