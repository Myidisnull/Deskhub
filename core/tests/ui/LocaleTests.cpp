#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/Brand.h"
#include "deskhub/ui/Locale.h"
#include "deskhub/ui/Strings.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace deskhub;

namespace {

void TestLanguageCodesRoundTrip() {
    std::printf("[locale] language codes parse and serialize...\n");
    ui::UiLanguage out = ui::UiLanguage::En;
    Check(ui::TryParseLanguageCode("", out) && out == ui::UiLanguage::System, "empty is system");
    Check(ui::TryParseLanguageCode("system", out) && out == ui::UiLanguage::System,
        "system keyword");
    Check(ui::TryParseLanguageCode("zh-CN", out) && out == ui::UiLanguage::ZhHans, "zh-CN");
    Check(ui::TryParseLanguageCode("fr-FR", out) && out == ui::UiLanguage::Fr, "fr-FR");
    Check(ui::TryParseLanguageCode("de", out) && out == ui::UiLanguage::De, "de");
    Check(ui::TryParseLanguageCode("ru", out) && out == ui::UiLanguage::Ru, "ru");
    Check(ui::TryParseLanguageCode("ja", out) && out == ui::UiLanguage::Ja, "ja");
    Check(ui::TryParseLanguageCode("ko-KR", out) && out == ui::UiLanguage::Ko, "ko-KR");
    Check(ui::TryParseLanguageCode("ar", out) && out == ui::UiLanguage::Ar, "ar");
    Check(!ui::TryParseLanguageCode("xx", out), "unknown rejected");
    Check(std::strcmp(ui::LanguageCode(ui::UiLanguage::ZhHans), "zh-Hans") == 0, "zh-Hans code");
}

void TestResolveFollowsSystemTag() {
    std::printf("[locale] system preference resolves from the OS tag...\n");
    Check(ui::ResolveLanguage(ui::UiLanguage::System, "zh-CN") == ui::UiLanguage::ZhHans,
        "system+zh-CN");
    Check(ui::ResolveLanguage(ui::UiLanguage::System, "fr_FR.UTF-8") == ui::UiLanguage::Fr,
        "system+fr_FR");
    Check(ui::ResolveLanguage(ui::UiLanguage::System, "weird") == ui::UiLanguage::En,
        "unknown system falls back to English");
    Check(ui::ResolveLanguage(ui::UiLanguage::Ja, "zh-CN") == ui::UiLanguage::Ja,
        "explicit preference wins");
}

void TestTranslateSwitchesCatalog() {
    std::printf("[locale] Translate looks up the active catalog...\n");
    const ui::UiLanguage previous = ui::CurrentUiLanguage();
    ui::SetUiLanguage(ui::UiLanguage::En);
    Check(std::strcmp(ui::kAppTitle, brand::kProductName) == 0, "English title");
    ui::SetUiLanguage(ui::UiLanguage::ZhHans);
    Check(std::strcmp(ui::kAppTitle, brand::kProductName) == 0, "Chinese title stays English");
    Check(std::strcmp(ui::kBackgroundPromptYes, "是") == 0, "Chinese yes");
    ui::SetUiLanguage(previous);
}

}

void RunLocaleTests() {
    TestLanguageCodesRoundTrip();
    TestResolveFollowsSystemTag();
    TestTranslateSwitchesCatalog();
}
