#include "deskhub/ui/Locale.h"

#include "LocaleInternal.h"
#include "deskhub/ui/Brand.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace deskhub::ui {

namespace {

UiLanguage g_language = UiLanguage::En;
thread_local std::string g_brandSlots[8];
thread_local size_t g_brandSlot = 0;

bool HasBrandToken(const char* text) noexcept {
    return std::strstr(text, brand::kAppToken) != nullptr ||
           std::strstr(text, brand::kServiceToken) != nullptr;
}

void ReplaceAll(std::string& text, std::string_view from, std::string_view to) {
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

const char* ExpandBrandTokens(const char* text) noexcept {
    if (!text || !HasBrandToken(text)) return text ? text : "";
    std::string& slot = g_brandSlots[g_brandSlot++ % 8];
    slot.assign(text);
    ReplaceAll(slot, brand::kAppToken, brand::kProductName);
    ReplaceAll(slot, brand::kServiceToken, brand::kWindowsServiceName);
    return slot.c_str();
}

bool EntryKeyLess(const CatalogEntry& a, const char* key) {
    return std::strcmp(a.en, key) < 0;
}

const char* Lookup(const CatalogEntry* begin, const CatalogEntry* end, const char* english) {
    const CatalogEntry* it = std::lower_bound(begin, end, english, EntryKeyLess);
    if (it == end || std::strcmp(it->en, english) != 0) return english;
    return it->tr;
}

std::string_view PrimaryTag(std::string_view tag) {
    size_t end = 0;
    while (end < tag.size()) {
        const char c = tag[end];
        if (c == '-' || c == '_' || c == '.') break;
        ++end;
    }
    return tag.substr(0, end);
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = char(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = char(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

bool StartsWithIgnoreCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    return EqualsIgnoreCase(s.substr(0, prefix.size()), prefix);
}

}

const LanguageOption* LanguageOptions() noexcept {
    static const LanguageOption kOptions[kLanguageOptionCount] = {
        {UiLanguage::System, "", "System default"},
        {UiLanguage::En, "en", "English"},
        {UiLanguage::ZhHans, "zh-Hans", "简体中文"},
        {UiLanguage::Fr, "fr", "Français"},
        {UiLanguage::De, "de", "Deutsch"},
        {UiLanguage::Ru, "ru", "Русский"},
        {UiLanguage::Ja, "ja", "日本語"},
        {UiLanguage::Ko, "ko", "한국어"},
        {UiLanguage::Ar, "ar", "العربية"},
    };
    return kOptions;
}

UiLanguage ParseLanguageCode(std::string_view code) noexcept {
    UiLanguage out = UiLanguage::System;
    if (!TryParseLanguageCode(code, out)) return UiLanguage::System;
    return out;
}

bool TryParseLanguageCode(std::string_view code, UiLanguage& out) noexcept {
    if (code.empty() || EqualsIgnoreCase(code, "system")) {
        out = UiLanguage::System;
        return true;
    }
    if (EqualsIgnoreCase(code, "en") || StartsWithIgnoreCase(code, "en-") ||
        StartsWithIgnoreCase(code, "en_")) {
        out = UiLanguage::En;
        return true;
    }
    if (EqualsIgnoreCase(code, "zh-Hans") || EqualsIgnoreCase(code, "zh-CN") ||
        EqualsIgnoreCase(code, "zh-SG") || EqualsIgnoreCase(code, "zh") ||
        StartsWithIgnoreCase(code, "zh-") || StartsWithIgnoreCase(code, "zh_")) {
        out = UiLanguage::ZhHans;
        return true;
    }
    if (EqualsIgnoreCase(code, "fr") || StartsWithIgnoreCase(code, "fr-") ||
        StartsWithIgnoreCase(code, "fr_")) {
        out = UiLanguage::Fr;
        return true;
    }
    if (EqualsIgnoreCase(code, "de") || StartsWithIgnoreCase(code, "de-") ||
        StartsWithIgnoreCase(code, "de_")) {
        out = UiLanguage::De;
        return true;
    }
    if (EqualsIgnoreCase(code, "ru") || StartsWithIgnoreCase(code, "ru-") ||
        StartsWithIgnoreCase(code, "ru_")) {
        out = UiLanguage::Ru;
        return true;
    }
    if (EqualsIgnoreCase(code, "ja") || StartsWithIgnoreCase(code, "ja-") ||
        StartsWithIgnoreCase(code, "ja_")) {
        out = UiLanguage::Ja;
        return true;
    }
    if (EqualsIgnoreCase(code, "ko") || StartsWithIgnoreCase(code, "ko-") ||
        StartsWithIgnoreCase(code, "ko_")) {
        out = UiLanguage::Ko;
        return true;
    }
    if (EqualsIgnoreCase(code, "ar") || StartsWithIgnoreCase(code, "ar-") ||
        StartsWithIgnoreCase(code, "ar_")) {
        out = UiLanguage::Ar;
        return true;
    }
    return false;
}

const char* LanguageCode(UiLanguage language) noexcept {
    switch (language) {
        case UiLanguage::System: return "";
        case UiLanguage::En: return "en";
        case UiLanguage::ZhHans: return "zh-Hans";
        case UiLanguage::Fr: return "fr";
        case UiLanguage::De: return "de";
        case UiLanguage::Ru: return "ru";
        case UiLanguage::Ja: return "ja";
        case UiLanguage::Ko: return "ko";
        case UiLanguage::Ar: return "ar";
    }
    return "";
}

const char* LanguageNativeName(UiLanguage language) noexcept {
    for (size_t i = 0; i < kLanguageOptionCount; ++i) {
        if (LanguageOptions()[i].language == language) return LanguageOptions()[i].nativeName;
    }
    return LanguageOptions()[0].nativeName;
}

UiLanguage ResolveLanguage(UiLanguage preferred, std::string_view systemTag) noexcept {
    if (preferred != UiLanguage::System) return preferred;
    const UiLanguage fromTag = ParseLanguageCode(systemTag);
    if (fromTag != UiLanguage::System) return fromTag;
    const std::string_view primary = PrimaryTag(systemTag);
    const UiLanguage fromPrimary = ParseLanguageCode(primary);
    if (fromPrimary != UiLanguage::System) return fromPrimary;
    return UiLanguage::En;
}

void SetUiLanguage(UiLanguage effective) noexcept {
    if (effective == UiLanguage::System) effective = UiLanguage::En;
    g_language = effective;
}

UiLanguage CurrentUiLanguage() noexcept {
    return g_language;
}

void ApplyUiLanguagePreference(std::string_view preferredCode, std::string_view systemTag) noexcept {
    SetUiLanguage(ResolveLanguage(ParseLanguageCode(preferredCode), systemTag));
}

const char* Translate(const char* english) noexcept {
    if (!english || !*english) return english ? english : "";
    const char* raw = english;
    if (g_language != UiLanguage::En) {
        size_t count = 0;
        const CatalogEntry* table = CatalogFor(g_language, count);
        if (table && count != 0) raw = Lookup(table, table + count, english);
    }
    return ExpandBrandTokens(raw);
}

}
