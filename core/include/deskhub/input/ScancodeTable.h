#pragma once
#include "deskhub/input/KeyMap.h"

#include <cstdint>
#include <span>

namespace deskhub {

inline constexpr int32_t kVkControl = 0x11;
inline constexpr int32_t kVkMenu = 0x12;
inline constexpr int32_t kVkLShift = 0xA0;
inline constexpr int32_t kVkRShift = 0xA1;
inline constexpr int32_t kVkLControl = 0xA2;
inline constexpr int32_t kVkRControl = 0xA3;
inline constexpr int32_t kVkLMenu = 0xA4;
inline constexpr int32_t kVkRMenu = 0xA5;

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
    int32_t scan;
};

template <class Native>
class ScancodeTable {
public:
    using Entry = ScancodeEntry<Native>;

    constexpr explicit ScancodeTable(std::span<const Entry> entries) : entries_(entries) {}

    constexpr bool ToWindows(Native native, int32_t& vk, int32_t& scan) const {
        for (const Entry& e : entries_) {
            if (e.native != native) continue;
            vk = e.vk;
            scan = e.scan;
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
