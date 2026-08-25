#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/terminal/Repaint.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

term::Screen Replay(const term::Screen& source) {
    term::Screen copy(source.Size());
    copy.Write(term::RenderScreen(source));
    return copy;
}

bool SameGrid(const term::Screen& a, const term::Screen& b) {
    if (!(a.Size() == b.Size())) return false;
    for (uint16_t row = 0; row < a.Size().rows; ++row)
        for (uint16_t col = 0; col < a.Size().cols; ++col)
            if (!(a.At(row, col) == b.At(row, col))) return false;
    return true;
}

void TestRepaintCarriesTextAndColour() {
    std::printf("[repaint] a repaint puts every cell back where it was...\n");
    term::Screen source(TermSize{20, 4});
    source.Write("plain\r\n");
    source.Write("\x1b[1;31mred bold\x1b[0m\r\n");
    source.Write("\x1b[48;2;10;20;30mrgb background\x1b[0m");

    const term::Screen copy = Replay(source);
    Check(SameGrid(source, copy), "every cell, glyph and pen alike, survives the round trip");
    Check(copy.Cursor() == source.Cursor(), "and the cursor lands where it was");
    Check(copy.CurrentPen() == source.CurrentPen(), "with the pen still loaded for what comes next");
}

void TestRepaintCarriesEveryColourAndAttribute() {
    std::printf("[repaint] bright, 256-colour and every attribute all come back...\n");
    term::Screen source(TermSize{40, 5});
    source.Write("\x1b[90mbright foreground\x1b[0m\r\n");
    source.Write("\x1b[101mbright background\x1b[0m\r\n");
    source.Write("\x1b[38;5;200;48;5;27mtwo palette colours\x1b[0m\r\n");
    source.Write("\x1b[1;2;3;4;5;7;8;9mevery attribute at once\x1b[0m\r\n");
    source.Write("\x1b[38;2;1;2;3;48;2;4;5;6mtrue colour\x1b[0m");

    const term::Screen copy = Replay(source);
    Check(SameGrid(source, copy), "no colour or attribute is lost on the way back");
}

void TestRepaintCarriesModesAndRegion() {
    std::printf("[repaint] a repaint restores the modes a full-screen app set...\n");
    term::Screen source(TermSize{30, 8});
    source.Write("\x1b[?1049h");
    source.Write("\x1b[2;6r");
    source.Write("\x1b[?1h\x1b[?2004h\x1b[?7l\x1b=");
    source.Write("\x1b[3;4Hinside the region");

    const term::Screen copy = Replay(source);
    Check(SameGrid(source, copy), "the alternate screen comes back cell for cell");
    Check(copy.AlternateScreen(), "still on the alternate screen");
    Check(copy.Region() == source.Region(), "with the scroll region the app asked for");
    Check(copy.Modes() == source.Modes(), "and every mode the app switched on");
    Check(copy.Cursor() == source.Cursor(), "cursor included");
}

void TestRepaintCarriesOriginModeCursor() {
    std::printf("[repaint] origin mode does not shift the cursor on the way back...\n");
    term::Screen source(TermSize{20, 10});
    source.Write("\x1b[3;8r\x1b[?6h");
    source.Write("\x1b[2;5Hhere");

    const term::Screen copy = Replay(source);
    Check(copy.Modes().origin, "origin mode is back on");
    Check(copy.Region() == source.Region(), "over the same region");
    Check(copy.Cursor() == source.Cursor(), "and the cursor sits on the same absolute cell");
    Check(SameGrid(source, copy), "with the text where it was written");
}

void TestRepaintSurvivesLineDrawingCharset() {
    std::printf("[repaint] a repaint does not mangle line-drawing glyphs...\n");
    term::Screen source(TermSize{12, 2});
    source.Write("\x1b(0lqqqk\x1b(B ascii");

    const term::Screen copy = Replay(source);
    Check(SameGrid(source, copy), "the drawn box characters come back as themselves");
    Check(copy.Charset() == source.Charset(), "and the charset state is handed back unchanged");
}

void TestRepaintKeepsShiftedOutCharset() {
    std::printf("[repaint] a shell left in graphics mode stays there...\n");
    term::Screen source(TermSize{12, 2});
    source.Write("\x1b)0\x0eqqq");

    const term::Screen copy = Replay(source);
    Check(copy.Charset() == source.Charset(), "G1 graphics and shift-out both survive");
    Check(SameGrid(source, copy), "and what was drawn stays drawn");

    term::Screen after = Replay(source);
    after.Write("q");
    source.Write("q");
    Check(SameGrid(source, after), "bytes that arrive after the repaint translate the same way");
}

void TestRepaintCarriesTitleAndCursorVisibility() {
    std::printf("[repaint] the title and a hidden cursor come along...\n");
    term::Screen source(TermSize{16, 3});
    source.Write("\x1b]0;build running\x07");
    source.Write("\x1b[?25l");

    const term::Screen copy = Replay(source);
    Check(copy.Title() == source.Title(), "the window title is restored");
    Check(!copy.Cursor().visible, "and a hidden cursor stays hidden");
}

void TestRepaintOfABlankScreenClearsAStaleOne() {
    std::printf("[repaint] a repaint wipes whatever the client had before...\n");
    term::Screen source(TermSize{10, 3});
    source.Write("fresh");

    term::Screen stale(TermSize{10, 3});
    stale.Write("\x1b[41mold noise everywhere\r\nsecond line\r\nthird line");
    stale.Write(term::RenderScreen(source));

    Check(SameGrid(source, stale), "nothing of the old screen shows through");
}

}

void RunRepaintTests() {
    TestRepaintCarriesTextAndColour();
    TestRepaintCarriesEveryColourAndAttribute();
    TestRepaintCarriesModesAndRegion();
    TestRepaintCarriesOriginModeCursor();
    TestRepaintSurvivesLineDrawingCharset();
    TestRepaintKeepsShiftedOutCharset();
    TestRepaintCarriesTitleAndCursorVisibility();
    TestRepaintOfABlankScreenClearsAStaleOne();
}
