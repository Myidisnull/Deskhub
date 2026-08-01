#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/input/ScancodeTable.h"

#include <cstdio>

using namespace deskhub;

namespace {

using Entry = ScancodeEntry<uint16_t>;

constexpr int32_t kE0 = kScanExtended;

constexpr Entry kSample[] = {
    {30, 'A', 0x1E},
    {31, 'S', 0x1F},
    {42, kVkLShift, 0x2A},
    {54, kVkRShift, 0x36},
    {29, kVkLControl, 0x1D},
    {97, kVkRControl, 0x1D | kE0},
    {56, kVkLMenu, 0x38},
    {100, kVkRMenu, 0x38 | kE0},
    {103, 0x26, 0x48 | kE0},
};

constexpr ScancodeTable<uint16_t> kTable{kSample};

void TestNativeToWindows() {
    std::printf("[scancode] a native code yields both the vk and the scancode...\n");
    int32_t vk = 0, scan = 0;
    Check(kTable.ToWindows(30, vk, scan) && vk == 'A' && scan == 0x1E, "evdev 30 is 'A'");
    Check(kTable.ToWindows(103, vk, scan) && vk == 0x26 && scan == (0x48 | kE0),
        "arrow up keeps its extended-scancode bit");
    Check(!kTable.ToWindows(9999, vk, scan), "an unmapped code is reported, not guessed");

    vk = 0x7777;
    scan = 0x7777;
    Check(!kTable.ToWindows(9999, vk, scan) && vk == 0x7777 && scan == 0x7777,
        "and a failed lookup leaves the caller's variables untouched");
}

void TestWindowsToNative() {
    std::printf("[scancode] the reverse lookup finds the key that produces a vk...\n");
    uint16_t native = 0;
    Check(kTable.FromWindows('A', native) && native == 30, "'A' comes back as evdev 30");
    Check(kTable.FromWindows(kVkRShift, native) && native == 54, "right shift is its own key");
    Check(!kTable.FromWindows(0x7B, native), "a vk the table has no key for fails");
}

void TestGenericModifiersResolveToTheLeftKey() {
    std::printf("[scancode] a side-less modifier is injected as the left-hand key...\n");
    uint16_t native = 0;
    Check(kTable.FromWindows(kVkShift, native) && native == 42, "generic shift -> left shift");
    Check(kTable.FromWindows(kVkControl, native) && native == 29, "generic ctrl -> left ctrl");
    Check(kTable.FromWindows(kVkMenu, native) && native == 56, "generic alt -> left alt");

    Check(kTable.FromWindows(kVkLShift, native) && native == 42, "an explicit side is respected");
    Check(kTable.FromWindows(kVkRShift, native) && native == 54, "on both sides");

    Check(PreferLeftModifier('A') == 'A', "an ordinary vk passes through untouched");
    Check(PreferLeftModifier(kVkRControl) == kVkRControl, "so does an already-sided one");
}

void TestRoundTrip() {
    std::printf("[scancode] every entry survives a native -> windows -> native round trip...\n");
    bool ok = true;
    for (const Entry& e : kTable.entries()) {
        int32_t vk = 0, scan = 0;
        if (!kTable.ToWindows(e.native, vk, scan)) {
            ok = false;
            continue;
        }
        uint16_t back = 0;
        if (!kTable.FromWindows(vk, back)) {
            ok = false;
            continue;
        }
        int32_t vk2 = 0, scan2 = 0;
        if (!kTable.ToWindows(back, vk2, scan2) || vk2 != vk || scan2 != scan) ok = false;
    }
    Check(ok, "the vk a key produces always maps back to a key producing that same vk");
}

void TestFirstMatchWins() {
    std::printf("[scancode] with duplicate vks the first entry wins, deterministically...\n");
    static constexpr Entry dupes[] = {
        {1, 'X', 0x11},
        {2, 'X', 0x22},
    };
    constexpr ScancodeTable<uint16_t> table{dupes};
    uint16_t native = 0;
    Check(table.FromWindows('X', native) && native == 1, "the earlier row is chosen");
}

void TestCanonicalScancodes() {
    std::printf("[scancode] the shared vk -> set-1 table matches the PC/AT layout...\n");
    Check(VkToSet1Scancode('A') == 0x1E && VkToSet1Scancode('Z') == 0x2C, "letters");
    Check(VkToSet1Scancode('1') == 0x02 && VkToSet1Scancode('0') == 0x0B, "digits");
    Check(VkToSet1Scancode(kVkF1) == 0x3B && VkToSet1Scancode(kVkF1 + 9) == 0x44 &&
              VkToSet1Scancode(kVkF1 + 10) == 0x57 && VkToSet1Scancode(kVkF1 + 11) == 0x58,
        "function keys including the split F11/F12 block");
    Check(VkToSet1Scancode(kVkUp) == (0x48 | kE0) &&
              VkToSet1Scancode(kVkDelete) == (0x53 | kE0),
        "navigation keys carry the extended bit");
    Check(VkToSet1Scancode(kVkNumpad0) == 0x52 && VkToSet1Scancode(kVkNumpad0 + 7) == 0x47,
        "the numpad digits are not the navigation keys");
    Check(VkToSet1Scancode(kVkControl) == 0x1D && VkToSet1Scancode(kVkLControl) == 0x1D &&
              VkToSet1Scancode(kVkRControl) == (0x1D | kE0),
        "modifiers map per side, generic falls to the left");
    Check(VkToSet1Scancode(kVkLWin) == (0x5B | kE0), "the win key is extended");
    Check(VkToSet1Scancode(0x07) == 0, "an unmapped vk yields no scancode");
}

void TestDerivedScanColumn() {
    std::printf("[scancode] a table row without a scan derives it from the vk...\n");
    static constexpr Entry derived[] = {
        {30, 'A'},
        {103, kVkUp},
        {96, kVkReturn, 0x1C | kE0},
    };
    constexpr ScancodeTable<uint16_t> table{derived};
    int32_t vk = 0, scan = 0;
    Check(table.ToWindows(30, vk, scan) && scan == 0x1E, "'A' derives 0x1E");
    Check(table.ToWindows(103, vk, scan) && scan == (0x48 | kE0),
        "arrow up derives its extended scancode");
    Check(table.ToWindows(96, vk, scan) && scan == (0x1C | kE0),
        "an explicit scan (numpad enter) still overrides the derived one");
}

void TestEmptyTable() {
    std::printf("[scancode] an empty table refuses everything instead of misbehaving...\n");
    const ScancodeTable<uint16_t> empty{std::span<const Entry>{}};
    int32_t vk = 0, scan = 0;
    uint16_t native = 0;
    Check(!empty.ToWindows(30, vk, scan), "no forward match");
    Check(!empty.FromWindows('A', native), "no reverse match");
    Check(empty.entries().empty(), "and it reports itself as empty");
}

}

void RunScancodeTableTests() {
    TestNativeToWindows();
    TestWindowsToNative();
    TestGenericModifiersResolveToTheLeftKey();
    TestRoundTrip();
    TestFirstMatchWins();
    TestCanonicalScancodes();
    TestDerivedScanColumn();
    TestEmptyTable();
}
