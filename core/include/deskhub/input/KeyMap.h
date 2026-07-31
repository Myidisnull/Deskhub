#pragma once
#include "deskhub/input/VirtualKeys.h"

#include <cstdint>
#include <optional>

namespace deskhub {

struct KeyChord {
    int32_t vk = 0;
    bool shift = false;
};

inline std::optional<KeyChord> CharToKeyChord(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return KeyChord{int32_t(cp - 'a' + 'A'), false};
    if (cp >= 'A' && cp <= 'Z') return KeyChord{int32_t(cp), true};
    if (cp >= '0' && cp <= '9') return KeyChord{int32_t(cp), false};

    switch (cp) {
        case '\b': return KeyChord{kVkBack, false};
        case '\t': return KeyChord{kVkTab, false};
        case '\n':
        case '\r': return KeyChord{kVkReturn, false};
        case ' ': return KeyChord{kVkSpace, false};

        case '!': return KeyChord{'1', true};
        case '@': return KeyChord{'2', true};
        case '#': return KeyChord{'3', true};
        case '$': return KeyChord{'4', true};
        case '%': return KeyChord{'5', true};
        case '^': return KeyChord{'6', true};
        case '&': return KeyChord{'7', true};
        case '*': return KeyChord{'8', true};
        case '(': return KeyChord{'9', true};
        case ')': return KeyChord{'0', true};

        case ';': return KeyChord{kVkOem1, false};
        case ':': return KeyChord{kVkOem1, true};
        case '=': return KeyChord{kVkOemPlus, false};
        case '+': return KeyChord{kVkOemPlus, true};
        case ',': return KeyChord{kVkOemComma, false};
        case '<': return KeyChord{kVkOemComma, true};
        case '-': return KeyChord{kVkOemMinus, false};
        case '_': return KeyChord{kVkOemMinus, true};
        case '.': return KeyChord{kVkOemPeriod, false};
        case '>': return KeyChord{kVkOemPeriod, true};
        case '/': return KeyChord{kVkOem2, false};
        case '?': return KeyChord{kVkOem2, true};
        case '`': return KeyChord{kVkOem3, false};
        case '~': return KeyChord{kVkOem3, true};
        case '[': return KeyChord{kVkOem4, false};
        case '{': return KeyChord{kVkOem4, true};
        case '\\': return KeyChord{kVkOem5, false};
        case '|': return KeyChord{kVkOem5, true};
        case ']': return KeyChord{kVkOem6, false};
        case '}': return KeyChord{kVkOem6, true};
        case '\'': return KeyChord{kVkOem7, false};
        case '"': return KeyChord{kVkOem7, true};
        default: return std::nullopt;
    }
}

}
