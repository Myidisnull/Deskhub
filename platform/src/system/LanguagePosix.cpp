#include "deskhubp/system/Language.h"

#include <cstdlib>

namespace deskhubp {

std::string SystemLanguageTag() {
    if (const char* lang = std::getenv("LC_ALL"); lang && *lang) return lang;
    if (const char* lang = std::getenv("LC_MESSAGES"); lang && *lang) return lang;
    if (const char* lang = std::getenv("LANG"); lang && *lang) return lang;
    return {};
}

}
