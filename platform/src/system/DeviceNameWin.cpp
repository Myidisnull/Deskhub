#include "deskhubp/system/DeviceName.h"

#include "deskhub/ui/UiSettings.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iterator>

namespace deskhubp {
namespace {

std::string Utf8Of(const wchar_t* w, DWORD len) {
    if (!len) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, int(len), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, int(len), out.data(), n, nullptr, nullptr);
    return out;
}

}

std::string LocalDeviceName() {
    wchar_t host[256];
    DWORD n = DWORD(std::size(host));
    if (GetComputerNameExW(ComputerNameDnsHostname, host, &n) && n)
        return deskhub::ui::TruncateDeviceName(Utf8Of(host, n));

    wchar_t user[256];
    n = GetEnvironmentVariableW(L"USERNAME", user, DWORD(std::size(user)));
    if (n && n < DWORD(std::size(user)))
        return deskhub::ui::TruncateDeviceName(Utf8Of(user, n));
    return {};
}

}
