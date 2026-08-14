#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace deskhub::ui {

enum class UiLanguage : uint8_t {
    System = 0,
    En,
    ZhHans,
    Fr,
    De,
    Ru,
    Ja,
    Ko,
    Ar,
};

struct LanguageOption {
    UiLanguage language;
    const char* code;
    const char* nativeName;
};

struct LStr {
    const char* en;
    constexpr explicit LStr(const char* s) noexcept
        : en(s) {}
    const char* get() const noexcept;
    const char* c_str() const noexcept {
        return get();
    }
    operator const char*() const noexcept {
        return get();
    }
    operator std::string_view() const noexcept {
        return get();
    }
};

inline std::string operator+(const std::string& a, LStr b) {
    return a + b.get();
}

inline std::string operator+(LStr a, const std::string& b) {
    return std::string(a.get()) + b;
}

inline std::string operator+(LStr a, const char* b) {
    return std::string(a.get()) + (b ? b : "");
}

inline std::string operator+(const char* a, LStr b) {
    return std::string(a ? a : "") + b.get();
}

inline std::string& operator+=(std::string& a, LStr b) {
    a += b.get();
    return a;
}

inline constexpr size_t kLanguageOptionCount = 9;

const LanguageOption* LanguageOptions() noexcept;
UiLanguage ParseLanguageCode(std::string_view code) noexcept;
bool TryParseLanguageCode(std::string_view code, UiLanguage& out) noexcept;
const char* LanguageCode(UiLanguage language) noexcept;
const char* LanguageNativeName(UiLanguage language) noexcept;
UiLanguage ResolveLanguage(UiLanguage preferred, std::string_view systemTag) noexcept;
void SetUiLanguage(UiLanguage effective) noexcept;
UiLanguage CurrentUiLanguage() noexcept;
void ApplyUiLanguagePreference(std::string_view preferredCode, std::string_view systemTag) noexcept;
const char* Translate(const char* english) noexcept;

inline const char* LStr::get() const noexcept {
    return Translate(en);
}

}
