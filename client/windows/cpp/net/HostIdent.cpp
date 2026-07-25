// =============================================================================
// HostIdent.cpp — cài đặt: đọc MachineGuid, băm FNV-1a xuống 32 bit, nhớ kết quả.
//
// FNV-1a chứ không phải hàm băm mã hoá: đây là khoá GỘP DÒNG trong một danh sách
// tối đa 32 máy, không phải định danh bảo mật (xem header). Va chạm ở cỡ đó thực
// tế bằng 0, và FNV-1a không kéo theo thư viện nào.
//
// LIÊN QUAN: net/HostIdent.h (vì sao phải ổn định qua các lần chạy)
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "net/HostIdent.h"

#include <windows.h>

namespace {

uint32_t Fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

// HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid — Windows sinh lúc cài đặt.
// Đọc bằng KEY_WOW64_64KEY: tiến trình 32-bit mà không có cờ này sẽ bị chuyển hướng
// sang nhánh Wow6432Node, nơi khoá đó không tồn tại.
std::string ReadMachineGuid() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
            KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};

    wchar_t buf[128]{};
    DWORD cb = sizeof(buf), type = 0;
    const LSTATUS r = RegQueryValueExW(key, L"MachineGuid", nullptr, &type,
        reinterpret_cast<LPBYTE>(buf), &cb);
    RegCloseKey(key);
    if (r != ERROR_SUCCESS || type != REG_SZ) return {};

    std::string out;
    for (const wchar_t* p = buf; *p; ++p) out += char(*p); // GUID là ASCII thuần
    return out;
}

std::string ReadComputerName() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(buf, &n)) return "PC";
    const int need = WideCharToMultiByte(CP_UTF8, 0, buf, int(n), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return "PC";
    std::string s(size_t(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, int(n), s.data(), need, nullptr, nullptr);
    return s;
}

} // namespace

uint32_t LocalHostId() {
    // Khởi tạo biến static cục bộ là thread-safe từ C++11 — không cần khoá tay.
    static const uint32_t id = [] {
        const std::string guid = ReadMachineGuid();
        uint32_t h = Fnv1a(guid.empty() ? LocalHostName() : guid);
        if (!h) h = 1; // 0 dành cho "chưa biết" ở phía đọc
        return h;
    }();
    return id;
}

const std::string& LocalHostName() {
    static const std::string name = ReadComputerName();
    return name;
}
