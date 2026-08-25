#pragma once
#include "deskhub/terminal/Screen.h"

#include <cstdint>

namespace deskhub::term {

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    bool operator==(const Rgb&) const = default;
};

inline constexpr Rgb kDefaultForeground{0xD0, 0xD0, 0xD0};
inline constexpr Rgb kDefaultBackground{0x10, 0x12, 0x18};
inline constexpr Rgb kCursorColor{0xE0, 0xE0, 0xE0};

Rgb PaletteRgb(uint8_t index);
Rgb ResolveColor(const Color& color, bool foreground);

struct CellColors {
    Rgb fg{};
    Rgb bg{};

    bool operator==(const CellColors&) const = default;
};

CellColors ResolveCell(const Pen& pen, bool screenReverse);

}
