#pragma once
#include "deskhub/input/KeyMap.h"
#include "deskhub/input/Set1Scancodes.h"
#include "deskhub/input/VirtualKeys.h"

#include <cstdint>
#include <span>

namespace deskhub {

constexpr int32_t PreferLeftModifier(int32_t vk) {
    switch (vk) {
        case kVkShift: return kVkLShift;
        case kVkControl: return kVkLControl;
        case kVkMenu: return kVkLMenu;
        default: return vk;
    }
}

template <class Native>
struct ScancodeEntry {
    Native native;
    int32_t vk;
    int32_t scan = 0;
};

template <class Native>
class ScancodeTable {
public:
    using Entry = ScancodeEntry<Native>;

    constexpr explicit ScancodeTable(std::span<const Entry> entries)
        : entries_(entries) {}

    constexpr bool ToWindows(Native native, int32_t& vk, int32_t& scan) const {
        for (const Entry& e : entries_) {
            if (e.native != native) continue;
            vk = e.vk;
            scan = e.scan ? e.scan : VkToSet1Scancode(e.vk);
            return true;
        }
        return false;
    }

    constexpr bool FromWindows(int32_t vk, Native& native) const {
        const int32_t wanted = PreferLeftModifier(vk);
        for (const Entry& e : entries_) {
            if (e.vk != wanted) continue;
            native = e.native;
            return true;
        }
        return false;
    }

    constexpr std::span<const Entry> entries() const {
        return entries_;
    }

private:
    std::span<const Entry> entries_;
};

}
