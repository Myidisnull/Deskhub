#include "deskhubp/system/Language.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace deskhubp {

std::string SystemLanguageTag() {
    wchar_t name[85] = {};
    const int n = GetUserDefaultLocaleName(name, 85);
    if (n <= 1) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, name, n - 1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name, n - 1, out.data(), bytes, nullptr, nullptr);
    return out;
}

}
