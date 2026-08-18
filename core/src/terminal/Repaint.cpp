#include "deskhub/terminal/Repaint.h"

#include <string>
#include <string_view>

namespace deskhub::term {

namespace {

constexpr char kEsc = '\x1b';
constexpr char kBell = '\x07';
constexpr char kShiftIn = '\x0f';
constexpr char kShiftOut = '\x0e';

void AppendCsi(std::string& out, std::string_view body) {
    out += kEsc;
    out += '[';
    out += body;
}

void AppendCup(std::string& out, uint16_t row, uint16_t col) {
    std::string body = std::to_string(unsigned(row) + 1);
    body += ';';
    body += std::to_string(unsigned(col) + 1);
    body += 'H';
    AppendCsi(out, body);
}

void AppendPrivateMode(std::string& out, unsigned mode, bool on) {
    std::string body = "?";
    body += std::to_string(mode);
    body += on ? 'h' : 'l';
    AppendCsi(out, body);
}

void AppendAnsiMode(std::string& out, unsigned mode, bool on) {
    std::string body = std::to_string(mode);
    body += on ? 'h' : 'l';
    AppendCsi(out, body);
}

void AppendSelectCharset(std::string& out, char slot, bool graphics) {
    out += kEsc;
    out += slot;
    out += graphics ? '0' : 'B';
}

void AppendColor(std::string& params, const Color& color, bool foreground) {
    const unsigned basic = foreground ? 30u : 40u;
    const unsigned bright = foreground ? 90u : 100u;
    const unsigned extended = foreground ? 38u : 48u;
    const unsigned none = foreground ? 39u : 49u;
    switch (color.kind) {
        case ColorKind::Default: params += ';' + std::to_string(none); return;
        case ColorKind::Palette:
            if (color.r < 8)
                params += ';' + std::to_string(basic + color.r);
            else if (color.r < 16)
                params += ';' + std::to_string(bright + color.r - 8u);
            else
                params += ';' + std::to_string(extended) + ";5;" + std::to_string(color.r);
            return;
        case ColorKind::Rgb:
            params += ';' + std::to_string(extended) + ";2;" + std::to_string(color.r) + ';'
                + std::to_string(color.g) + ';' + std::to_string(color.b);
            return;
    }
}

void AppendPen(std::string& out, const Pen& pen) {
    std::string params = "0";
    if ((pen.attrs & kAttrBold) != 0) params += ";1";
    if ((pen.attrs & kAttrDim) != 0) params += ";2";
    if ((pen.attrs & kAttrItalic) != 0) params += ";3";
    if ((pen.attrs & kAttrUnderline) != 0) params += ";4";
    if ((pen.attrs & kAttrBlink) != 0) params += ";5";
    if ((pen.attrs & kAttrReverse) != 0) params += ";7";
    if ((pen.attrs & kAttrHidden) != 0) params += ";8";
    if ((pen.attrs & kAttrStrike) != 0) params += ";9";
    AppendColor(params, pen.fg, true);
    AppendColor(params, pen.bg, false);
    params += 'm';
    AppendCsi(out, params);
}

void AppendGrid(std::string& out, const Screen& screen) {
    const TermSize size = screen.Size();
    Pen pen{};
    AppendPen(out, pen);
    for (uint16_t row = 0; row < size.rows; ++row) {
        AppendCup(out, row, 0);
        for (uint16_t col = 0; col < size.cols; ++col) {
            const Cell& cell = screen.At(row, col);
            if (!(cell.pen == pen)) {
                pen = cell.pen;
                AppendPen(out, pen);
            }
            out += EncodeUtf8(cell.ch < U' ' ? U' ' : cell.ch);
        }
    }
}

void AppendScrollRegion(std::string& out, ScrollRegion region, uint16_t rows) {
    std::string body;
    if (region.top != 0 || unsigned(region.bottom) + 1 != rows) {
        body = std::to_string(unsigned(region.top) + 1);
        body += ';';
        body += std::to_string(unsigned(region.bottom) + 1);
    }
    body += 'r';
    AppendCsi(out, body);
}

}

std::string RenderScreen(const Screen& screen) {
    const TermSize size = screen.Size();
    const TerminalModes& modes = screen.Modes();
    const ScrollRegion region = screen.Region();
    const CharsetState charset = screen.Charset();
    const CursorState cursor = screen.Cursor();

    std::string out;
    AppendPrivateMode(out, 1049, screen.AlternateScreen());
    out += kShiftIn;
    AppendSelectCharset(out, '(', false);
    AppendSelectCharset(out, ')', false);
    AppendAnsiMode(out, 4, false);
    AppendPrivateMode(out, 6, false);
    AppendPrivateMode(out, 7, false);
    AppendCsi(out, "r");
    AppendCsi(out, "H");
    AppendCsi(out, "2J");

    AppendGrid(out, screen);

    AppendPen(out, Pen{});
    if (!screen.Title().empty()) {
        out += kEsc;
        out += "]0;";
        out += screen.Title();
        out += kBell;
    }
    AppendPrivateMode(out, 5, modes.reverseVideo);
    AppendPrivateMode(out, 1, modes.applicationCursor);
    AppendPrivateMode(out, 2004, modes.bracketedPaste);
    AppendPrivateMode(out, 1000, modes.mouseReporting);
    out += kEsc;
    out += modes.applicationKeypad ? '=' : '>';
    AppendAnsiMode(out, 4, modes.insert);

    AppendScrollRegion(out, region, size.rows);
    AppendPrivateMode(out, 6, modes.origin);
    const uint16_t cursorRow = modes.origin && cursor.row >= region.top
        ? uint16_t(cursor.row - region.top)
        : cursor.row;
    AppendCup(out, cursorRow, cursor.col);

    AppendPrivateMode(out, 7, modes.autoWrap);
    AppendPrivateMode(out, 25, cursor.visible);
    if (charset.graphicsG0) AppendSelectCharset(out, '(', true);
    if (charset.graphicsG1) AppendSelectCharset(out, ')', true);
    if (charset.shiftedOut) out += kShiftOut;
    AppendPen(out, screen.CurrentPen());
    return out;
}

}
