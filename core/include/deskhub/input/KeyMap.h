#pragma once
#include <cstdint>
#include <optional>

namespace deskhub {

inline constexpr int32_t kVkBack = 0x08;
inline constexpr int32_t kVkTab = 0x09;
inline constexpr int32_t kVkReturn = 0x0D;
inline constexpr int32_t kVkShift = 0x10;
inline constexpr int32_t kVkSpace = 0x20;

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

        case ';': return KeyChord{0xBA, false};
        case ':': return KeyChord{0xBA, true};
        case '=': return KeyChord{0xBB, false};
        case '+': return KeyChord{0xBB, true};
        case ',': return KeyChord{0xBC, false};
        case '<': return KeyChord{0xBC, true};
        case '-': return KeyChord{0xBD, false};
        case '_': return KeyChord{0xBD, true};
        case '.': return KeyChord{0xBE, false};
        case '>': return KeyChord{0xBE, true};
        case '/': return KeyChord{0xBF, false};
        case '?': return KeyChord{0xBF, true};
        case '`': return KeyChord{0xC0, false};
        case '~': return KeyChord{0xC0, true};
        case '[': return KeyChord{0xDB, false};
        case '{': return KeyChord{0xDB, true};
        case '\\': return KeyChord{0xDC, false};
        case '|': return KeyChord{0xDC, true};
        case ']': return KeyChord{0xDD, false};
        case '}': return KeyChord{0xDD, true};
        case '\'': return KeyChord{0xDE, false};
        case '"': return KeyChord{0xDE, true};
        default: return std::nullopt;
    }
}

}
