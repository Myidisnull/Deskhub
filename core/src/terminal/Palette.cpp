#include "deskhub/terminal/Palette.h"

namespace deskhub::term {

namespace {

constexpr Rgb kAnsi[16] = {
    {0x1C, 0x1C, 0x1C},
    {0xCC, 0x33, 0x33},
    {0x33, 0xAA, 0x55},
    {0xCC, 0xAA, 0x33},
    {0x33, 0x77, 0xDD},
    {0xAA, 0x55, 0xCC},
    {0x33, 0xAA, 0xAA},
    {0xD0, 0xD0, 0xD0},
    {0x55, 0x55, 0x55},
    {0xFF, 0x55, 0x55},
    {0x55, 0xDD, 0x77},
    {0xFF, 0xDD, 0x55},
    {0x55, 0x99, 0xFF},
    {0xDD, 0x77, 0xFF},
    {0x55, 0xDD, 0xDD},
    {0xFF, 0xFF, 0xFF},
};

constexpr uint8_t kCubeLevels[6] = {0, 95, 135, 175, 215, 255};
constexpr uint8_t kCubeBase = 16;
constexpr uint8_t kGrayBase = 232;
constexpr uint8_t kGrayStep = 10;
constexpr uint8_t kGrayStart = 8;

uint8_t Dim(uint8_t value) {
    return uint8_t(value / 2);
}

}

Rgb PaletteRgb(uint8_t index) {
    if (index < 16) return kAnsi[index];
    if (index < kGrayBase) {
        const uint8_t at = uint8_t(index - kCubeBase);
        return Rgb{kCubeLevels[(at / 36) % 6], kCubeLevels[(at / 6) % 6], kCubeLevels[at % 6]};
    }
    const uint8_t level = uint8_t(kGrayStart + (index - kGrayBase) * kGrayStep);
    return Rgb{level, level, level};
}

Rgb ResolveColor(const Color& color, bool foreground) {
    switch (color.kind) {
    case ColorKind::Palette: return PaletteRgb(color.r);
    case ColorKind::Rgb: return Rgb{color.r, color.g, color.b};
    case ColorKind::Default: break;
    }
    return foreground ? kDefaultForeground : kDefaultBackground;
}

CellColors ResolveCell(const Pen& pen, bool screenReverse) {
    CellColors out;
    out.fg = ResolveColor(pen.fg, true);
    out.bg = ResolveColor(pen.bg, false);

    if ((pen.attrs & kAttrBold) != 0 && pen.fg.kind == ColorKind::Palette && pen.fg.r < 8)
        out.fg = PaletteRgb(uint8_t(pen.fg.r + 8));
    if ((pen.attrs & kAttrDim) != 0) out.fg = Rgb{Dim(out.fg.r), Dim(out.fg.g), Dim(out.fg.b)};

    const bool reverse = ((pen.attrs & kAttrReverse) != 0) != screenReverse;
    if (reverse) {
        const Rgb swap = out.fg;
        out.fg = out.bg;
        out.bg = swap;
    }
    if ((pen.attrs & kAttrHidden) != 0) out.fg = out.bg;
    return out;
}

}
