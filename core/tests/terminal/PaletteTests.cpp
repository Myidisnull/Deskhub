#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/terminal/Palette.h"

#include <cstdio>
#include <set>

using namespace deskhub::term;

namespace {

void TestPaletteLayout() {
    std::printf("[palette] the 256 entries are the ones every terminal agrees on...\n");
    Check(PaletteRgb(0) == Rgb{0x1C, 0x1C, 0x1C}, "entry 0 is the dark one");
    Check(PaletteRgb(7) != PaletteRgb(15), "normal and bright white differ");
    Check(PaletteRgb(16) == Rgb{0, 0, 0}, "the colour cube starts at black");
    Check(PaletteRgb(231) == Rgb{255, 255, 255}, "and ends at white");
    Check(PaletteRgb(196) == Rgb{255, 0, 0}, "the pure red of the cube is where it should be");
    Check(PaletteRgb(46) == Rgb{0, 255, 0}, "and pure green");
    Check(PaletteRgb(21) == Rgb{0, 0, 255}, "and pure blue");
    Check(PaletteRgb(232) == Rgb{8, 8, 8}, "the grey ramp starts nearly black");
    Check(PaletteRgb(255) == Rgb{238, 238, 238}, "and ends nearly white");

    std::set<int> greys;
    for (int i = 232; i <= 255; ++i) {
        const Rgb grey = PaletteRgb(uint8_t(i));
        Check(grey.r == grey.g && grey.g == grey.b, "every ramp entry is a true grey");
        greys.insert(grey.r);
    }
    Check(greys.size() == 24, "and all 24 of them are distinct");
}

void TestResolve() {
    std::printf("[palette] a pen turns into the two colours a client paints with...\n");
    Pen plain;
    const CellColors defaults = ResolveCell(plain, false);
    Check(defaults.fg == kDefaultForeground && defaults.bg == kDefaultBackground,
        "a pen that was never set uses the defaults");

    Pen coloured;
    coloured.fg = PaletteColor(1);
    coloured.bg = RgbColor(10, 20, 30);
    const CellColors mixed = ResolveCell(coloured, false);
    Check(mixed.fg == PaletteRgb(1) && mixed.bg == Rgb{10, 20, 30},
        "palette and true colour both come through");

    Pen bold = coloured;
    bold.attrs = kAttrBold;
    Check(ResolveCell(bold, false).fg == PaletteRgb(9),
        "bold lifts one of the first eight colours to its bright twin");
    Pen boldTrue;
    boldTrue.fg = RgbColor(10, 20, 30);
    boldTrue.attrs = kAttrBold;
    Check(ResolveCell(boldTrue, false).fg == Rgb{10, 20, 30},
        "but leaves a true colour alone");

    Pen dim;
    dim.attrs = kAttrDim;
    const CellColors dimmed = ResolveCell(dim, false);
    Check(dimmed.fg.r < kDefaultForeground.r, "dim darkens the foreground");

    Pen reverse = coloured;
    reverse.attrs = kAttrReverse;
    const CellColors flipped = ResolveCell(reverse, false);
    Check(flipped.fg == mixed.bg && flipped.bg == mixed.fg, "reverse swaps the two");
    Check(ResolveCell(coloured, true) == flipped,
        "a screen-wide reverse does the same to a plain cell");
    Check(ResolveCell(reverse, true) == mixed, "and the two reverses cancel out");

    Pen hidden = coloured;
    hidden.attrs = kAttrHidden;
    const CellColors invisible = ResolveCell(hidden, false);
    Check(invisible.fg == invisible.bg, "hidden text is painted in its own background");

    Check(ResolveColor(Color{}, true) == kDefaultForeground, "the default foreground is readable");
    Check(ResolveColor(Color{}, false) == kDefaultBackground, "against the default background");
    Check(kCursorColor != kDefaultBackground, "and the cursor is visible against it");
}

}

void RunPaletteTests() {
    TestPaletteLayout();
    TestResolve();
}
