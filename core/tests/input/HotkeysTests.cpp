#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/input/Hotkeys.h"
#include "deskhub/input/ScancodeTable.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

using namespace deskhub;

namespace {

void TestEveryEntryIsSendable() {
    std::printf("[hotkeys] every row carries a key the host can actually inject...\n");
    bool labelled = true, keyed = true, scanned = true;
    for (const Hotkey& h : TouchHotkeys()) {
        if (!h.label || !*h.label) labelled = false;
        if (!h.vk) keyed = false;
        if (!h.scan) scanned = false;
    }
    Check(!TouchHotkeys().empty(), "the shared bar is not empty");
    Check(labelled, "each row has a label to draw");
    Check(keyed, "each row has a virtual key");
    Check(scanned, "each row has a scancode");
}

void TestLabelsAreUnique() {
    std::printf("[hotkeys] no two buttons carry the same caption...\n");
    std::set<std::string> seen;
    bool unique = true;
    for (const Hotkey& h : TouchHotkeys())
        if (!seen.insert(h.label).second) unique = false;
    Check(unique, "labels are distinct");
    Check(seen.size() == TouchHotkeys().size(), "and the set covers every row");
}

void TestModifiersComeInPairs() {
    std::printf("[hotkeys] a chord names both the modifier vk and its scancode...\n");
    bool paired = true;
    for (const Hotkey& h : TouchHotkeys())
        if (h.hasModifier() != (h.modScan != 0)) paired = false;
    Check(paired, "modVk and modScan are set together or not at all");

    int chords = 0;
    for (const Hotkey& h : TouchHotkeys())
        if (h.hasModifier()) ++chords;
    Check(chords > 0, "the bar offers at least one chord");
}

void TestArrowsCarryTheExtendedBit() {
    std::printf("[hotkeys] navigation keys keep the extended-scancode bit...\n");
    bool extended = true;
    for (const Hotkey& h : TouchHotkeys()) {
        const bool navigation = h.vk == kVkUp || h.vk == kVkDown || h.vk == kVkLeft ||
                                h.vk == kVkRight || h.vk == kVkDelete;
        if (navigation && !(h.scan & kScanExtended)) extended = false;
    }
    Check(extended, "arrows and Delete are sent as extended keys");

    bool plainStaysPlain = true;
    for (const Hotkey& h : TouchHotkeys()) {
        const bool plain = h.vk == kVkEscape || h.vk == kVkTab || h.vk == kVkReturn;
        if (plain && (h.scan & kScanExtended)) plainStaysPlain = false;
    }
    Check(plainStaysPlain, "Esc/Tab/Enter are not");
}

struct RecordingQueue {
    std::string calls;

    void QueueKeyTap(int32_t vk, int32_t scan) {
        calls += "tap(" + std::to_string(vk) + "," + std::to_string(scan) + ")";
    }

    void QueueKeyChord(int32_t modVk, int32_t modScan, int32_t vk, int32_t scan) {
        calls += "chord(" + std::to_string(modVk) + "," + std::to_string(modScan) + "," +
                 std::to_string(vk) + "," + std::to_string(scan) + ")";
    }
};

void TestDispatchPicksTapOrChord() {
    std::printf("[hotkeys] a bar button becomes a tap, or a chord when it names a modifier...\n");

    RecordingQueue plain;
    DispatchHotkey(plain, Hotkey{"Esc", 27, 1});
    Check(plain.calls == "tap(27,1)", "a row without a modifier taps the key");

    RecordingQueue chord;
    DispatchHotkey(chord, Hotkey{"Ctrl+C", 'C', 46, 17, 29});
    Check(chord.calls == "chord(17,29,67,46)", "a row with a modifier sends the chord");

    RecordingQueue bar;
    for (const Hotkey& h : TouchHotkeys()) DispatchHotkey(bar, h);
    Check(!bar.calls.empty(), "every row on the shared bar dispatches to something");
}

}

void RunHotkeysTests() {
    TestEveryEntryIsSendable();
    TestLabelsAreUnique();
    TestModifiersComeInPairs();
    TestArrowsCarryTheExtendedBit();
    TestDispatchPicksTapOrChord();
}
