#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/terminal/VtParser.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace deskhub::term;

namespace {

std::vector<VtEvent> ParseAll(std::string_view text) {
    VtParser parser;
    std::vector<VtEvent> out;
    parser.Parse(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()),
        out);
    return out;
}

std::string Printed(const std::vector<VtEvent>& events) {
    std::string out;
    for (const VtEvent& ev : events)
        if (ev.action == VtAction::Print) out += EncodeUtf8(ev.codepoint);
    return out;
}

void TestPlainTextAndControls() {
    std::printf("[vt] plain text prints, C0 bytes are handed over as controls...\n");
    const auto events = ParseAll("ab\r\n\tc");
    Check(Printed(events) == "abc", "the printable bytes come out as printed characters");
    size_t controls = 0;
    for (const VtEvent& ev : events)
        if (ev.action == VtAction::Execute) ++controls;
    Check(controls == 3, "CR, LF and TAB are reported as controls, not printed");
    Check(events[2].action == VtAction::Execute && events[2].final == '\r',
        "the control byte itself is carried through");

    Check(ParseAll("\x7F").empty(), "DEL in the ground state is dropped");
}

void TestUtf8Decoding() {
    std::printf("[vt] UTF-8 arrives one codepoint at a time, junk becomes U+FFFD...\n");
    Check(Printed(ParseAll("t\xC3\xA1i")) == "t\xC3\xA1i", "a two-byte sequence round-trips");
    Check(Printed(ParseAll("\xE1\xBA\xA1")) == "\xE1\xBA\xA1", "so does a three-byte one");
    Check(Printed(ParseAll("\xF0\x9F\x98\x80")) == "\xF0\x9F\x98\x80",
        "and a four-byte emoji");

    const std::string replacement = EncodeUtf8(0xFFFD);
    Check(Printed(ParseAll("\xC3")) == "", "an unfinished sequence prints nothing yet");
    Check(Printed(ParseAll("\xC3\x41")) == replacement + "A",
        "a broken sequence is replaced and the stray byte still prints");
    Check(Printed(ParseAll("\x80")) == replacement, "a lone continuation byte is replaced");
    Check(Printed(ParseAll("\xC0\x80")) == replacement + replacement,
        "an overlong two-byte form is refused");
    Check(Printed(ParseAll("\xE0\x80\x80")) == replacement,
        "an overlong three-byte form is refused");
    Check(Printed(ParseAll("\xED\xA0\x80")) == replacement, "a surrogate is refused");
    Check(Printed(ParseAll("\xFF\xFE")) == replacement + replacement,
        "bytes that start no sequence at all are replaced");
    Check(Printed(ParseAll("\xC3\x1B[0m")) == replacement,
        "an escape in the middle of a sequence flushes it first");
    Check(Printed(ParseAll("\xC3\rx")) == replacement + "x",
        "so does a control byte");

    Check(EncodeUtf8(0x110000) == EncodeUtf8(0xFFFD),
        "a codepoint past the last one encodes as the replacement");
    Check(EncodeUtf8(0xD800) == EncodeUtf8(0xFFFD), "and so does a surrogate");
}

void TestUtf8SplitAcrossWrites() {
    std::printf("[vt] a codepoint split across two reads still arrives whole...\n");
    VtParser parser;
    std::vector<VtEvent> out;
    const uint8_t first[] = {0xE1, 0xBA};
    const uint8_t second[] = {0xA1};
    parser.Parse(first, out);
    Check(out.empty(), "nothing is printed until the last byte turns up");
    parser.Parse(second, out);
    Check(Printed(out) == "\xE1\xBA\xA1", "and then the whole character appears at once");
}

void TestCsiParsing() {
    std::printf("[vt] CSI parameters, private markers and intermediates...\n");
    {
        const auto ev = ParseAll("\x1B[H");
        Check(ev.size() == 1 && ev[0].action == VtAction::Csi && ev[0].final == 'H',
            "a bare CSI has its final byte");
        Check(ev[0].paramCount == 0 && ev[0].Param(0, 9) == 9,
            "and no parameters, so defaults apply");
    }
    {
        const auto ev = ParseAll("\x1B[12;34H");
        Check(ev.size() == 1 && ev[0].paramCount == 2 && ev[0].Param(0, 0) == 12 &&
                ev[0].Param(1, 0) == 34,
            "two parameters are read in order");
    }
    {
        const auto ev = ParseAll("\x1B[;5H");
        Check(ev.size() == 1 && ev[0].paramCount == 2 && ev[0].Param(0, 7) == 7 &&
                ev[0].Param(1, 0) == 5,
            "an omitted leading parameter falls back to the default");
    }
    {
        const auto ev = ParseAll("\x1B[?25l");
        Check(ev.size() == 1 && ev[0].prefix == '?' && ev[0].final == 'l' &&
                ev[0].Param(0, 0) == 25,
            "a private mode keeps its marker");
    }
    {
        const auto ev = ParseAll("\x1B[38:2::10:20:30m");
        Check(ev.size() == 1 && ev[0].paramCount == 6, "colon-separated parameters are read too");
        Check(ev[0].Param(2, -7) == -7, "an omitted colon field reads as its default");
        Check(ev[0].Param(3, 0) == 10 && ev[0].Param(5, 0) == 30, "and the rest line up");
    }
    {
        const auto ev = ParseAll("\x1B[4 q");
        Check(ev.size() == 1 && ev[0].intermediate == ' ' && ev[0].final == 'q',
            "an intermediate byte is kept beside the final one");
    }
    {
        const auto ev = ParseAll("\x1B[1;2<H");
        Check(ev.empty(), "a private marker after the parameters makes the whole sequence junk");
    }
    {
        const auto ev = ParseAll("\x1B[99999999999m");
        Check(ev.size() == 1 && ev[0].Param(0, 0) == kMaxVtParamValue,
            "an absurd parameter is clamped instead of overflowing");
    }
    {
        std::string many = "\x1B[";
        for (size_t i = 0; i < kMaxVtParams + 10; ++i) many += "1;";
        many += "m";
        const auto ev = ParseAll(many);
        Check(ev.size() == 1 && ev[0].paramCount <= kMaxVtParams,
            "more parameters than we hold are dropped, not written past the end");
    }
    {
        const auto ev = ParseAll("\x1B[1\rm");
        Check(ev.size() == 2 && ev[0].action == VtAction::Execute && ev[0].final == '\r',
            "a control byte inside a CSI is executed straight away");
        Check(ev[1].action == VtAction::Csi && ev[1].Param(0, 0) == 1,
            "and the sequence continues afterwards");
    }
    {
        const auto ev = ParseAll("\x1B[1\x1B[2m");
        Check(ev.size() == 1 && ev[0].Param(0, 0) == 2,
            "a new escape abandons the half-finished one");
    }
}

void TestEscParsing() {
    std::printf("[vt] two-byte and intermediate escapes...\n");
    {
        const auto ev = ParseAll("\x1B""7");
        Check(ev.size() == 1 && ev[0].action == VtAction::Esc && ev[0].final == '7',
            "ESC 7 is an escape dispatch");
    }
    {
        const auto ev = ParseAll("\x1B(0");
        Check(ev.size() == 1 && ev[0].intermediate == '(' && ev[0].final == '0',
            "a charset escape keeps its intermediate");
    }
    {
        const auto ev = ParseAll("\x1B#8");
        Check(ev.size() == 1 && ev[0].intermediate == '#' && ev[0].final == '8',
            "so does the alignment pattern");
    }
    {
        const auto ev = ParseAll("\x1B \x1B(B");
        Check(ev.size() == 1 && ev[0].intermediate == '(' && ev[0].final == 'B',
            "an escape restarted mid-intermediate keeps only the new one");
    }
    {
        const auto ev = ParseAll("\x1B\ra");
        Check(ev.size() == 2 && ev[0].action == VtAction::Execute,
            "a control byte after ESC is executed");
    }
    {
        const auto ev = ParseAll("\x1B(\rB");
        Check(ev.size() == 2 && ev[0].action == VtAction::Execute &&
                ev[1].action == VtAction::Esc,
            "and one inside an intermediate escape too");
    }
    {
        const auto ev = ParseAll("\x1B\x7F""7");
        Check(ev.size() == 1 && ev[0].final == '7', "DEL inside an escape is ignored");
    }
}

void TestOscParsing() {
    std::printf("[vt] OSC strings end at BEL or ST and are bounded...\n");
    {
        const auto ev = ParseAll("\x1B]0;my title\x07");
        Check(ev.size() == 1 && ev[0].action == VtAction::Osc && ev[0].Param(0, -1) == 0 &&
                ev[0].text == "my title",
            "a BEL-terminated OSC carries its command and text");
    }
    {
        const auto ev = ParseAll("\x1B]2;other\x1B\\");
        Check(ev.size() == 1 && ev[0].Param(0, -1) == 2 && ev[0].text == "other",
            "an ST-terminated one does the same");
    }
    {
        const auto ev = ParseAll("\x1B]notanumber\x07");
        Check(ev.size() == 1 && ev[0].Param(0, -1) == -1 && ev[0].text == "notanumber",
            "an OSC with no command number reports none");
    }
    {
        const auto ev = ParseAll("\x1B]0;half\x18rest");
        size_t dispatched = 0;
        for (const VtEvent& one : ev)
            if (one.action == VtAction::Osc) ++dispatched;
        Check(dispatched == 0, "CAN abandons the string without dispatching it");
        Check(Printed(ev) == "rest", "and what follows is ordinary text again");
    }
    {
        const auto ev = ParseAll("\x1B]0;a\x1B[1m");
        Check(ev.size() == 2 && ev[0].action == VtAction::Osc && ev[0].text == "a",
            "an escape that is not ST closes the string");
        Check(ev[1].action == VtAction::Csi && ev[1].final == 'm',
            "and the sequence that interrupted it is parsed");
    }
    {
        std::string huge = "\x1B]0;";
        huge.append(kMaxOscBytes + 500, 'x');
        huge += "\x07";
        const auto ev = ParseAll(huge);
        Check(ev.size() == 1 && ev[0].text.size() <= kMaxOscBytes,
            "a runaway OSC string is bounded instead of growing without limit");
    }
    {
        const auto ev = ParseAll("\x1BP1;2q...\x1B\\text");
        Check(Printed(ev) == "text",
            "a device control string is swallowed whole and printing resumes after it");
    }
    {
        const auto ev = ParseAll("\x1B^private\x07x");
        Check(Printed(ev) == "x", "so is a privacy message");
    }
}

void TestResetAndFuzz() {
    std::printf("[vt] a reset drops half-parsed state; junk never breaks the machine...\n");
    VtParser parser;
    std::vector<VtEvent> out;
    const uint8_t half[] = {0x1B, '[', '1', ';'};
    parser.Parse(half, out);
    parser.Reset();
    const uint8_t after[] = {'x'};
    parser.Parse(after, out);
    Check(Printed(out) == "x", "after a reset the next byte prints instead of joining a CSI");

    VtParser chaos;
    std::vector<VtEvent> events;
    for (int round = 0; round < 400; ++round) {
        std::vector<uint8_t> soup(Rnd() % 200);
        for (auto& b : soup) b = uint8_t(Rnd());
        events.clear();
        chaos.Parse(soup, events);
        for (const VtEvent& ev : events) {
            Check(ev.paramCount <= kMaxVtParams, "no event ever claims more parameters than we hold");
            Check(ev.text.size() <= kMaxOscBytes, "and no OSC text grows past the cap");
        }
    }
    Check(true, "400 rounds of random bytes left the parser standing");
}

}

void RunVtParserTests() {
    TestPlainTextAndControls();
    TestUtf8Decoding();
    TestUtf8SplitAcrossWrites();
    TestCsiParsing();
    TestEscParsing();
    TestOscParsing();
    TestResetAndFuzz();
}
