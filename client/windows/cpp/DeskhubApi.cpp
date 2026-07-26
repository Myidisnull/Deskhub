// =============================================================================
// DeskhubApi.cpp — cài đặt C API (xem DeskhubApi.h cho hợp đồng + nguyên tắc).
//
// M1: bọc hai hàm liệt kê có sẵn (ListLocalIPv4, ListCapturableWindows) thành API C.
// Việc duy nhất đáng nói là ĐỔI CHUỖI: lõi C++ dùng std::wstring (UTF-16) cho tên
// adapter/tiêu đề cửa sổ, nhưng ranh giới C# nhận UTF-8 — nên mọi wstring phải qua
// WideCharToMultiByte trước khi vào callback.
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "DeskhubApi.h"

#include <windows.h>
#include <string>

#include "net/NetInfo.h"
#include "capture/DisplayFinder.h"
#include "capture/WindowFinder.h"
#include "ElevatedShare.h" // IsProcessElevated
#include "net/Discovery.h" // ScanForHosts (GĐ9)
#include "net/Firewall.h"  // HostFirewallRulePresent

namespace {

// UTF-16 (wstring của lõi) -> UTF-8 (chuỗi C# nhận). Chuỗi rỗng trả rỗng.
std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0,
        nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace

DH_API int DH_CALL dh_list_local_ips(DhIpCallback cb, void* user) {
    const auto addrs = ListLocalIPv4();
    if (cb) {
        for (const auto& a : addrs) {
            const std::string name = ToUtf8(a.name);
            cb(name.c_str(), a.ip.c_str(), user); // a.ip đã là chuỗi ASCII/UTF-8
        }
    }
    return int(addrs.size());
}

DH_API int DH_CALL dh_list_windows(DhWindowCallback cb, void* user) {
    const auto wins = ListCapturableWindows();
    if (cb) {
        for (const auto& w : wins) {
            const std::string exe = ToUtf8(w.exeName);
            const std::string title = ToUtf8(w.title);
            cb(uint64_t(reinterpret_cast<uintptr_t>(w.hwnd)), exe.c_str(), title.c_str(),
                w.width, w.height, w.minimized ? 1 : 0, user);
        }
    }
    return int(wins.size());
}

DH_API int DH_CALL dh_list_displays(DhDisplayCallback cb, void* user) {
    const auto displays = ListDisplays();
    if (cb) {
        for (const auto& d : displays) {
            const std::string name = ToUtf8(d.name);
            cb(uint64_t(reinterpret_cast<uintptr_t>(d.monitor)), name.c_str(),
                d.width, d.height, d.primary ? 1 : 0, user);
        }
    }
    return int(displays.size());
}

DH_API int DH_CALL dh_discover_scan(uint16_t port, uint32_t timeoutMs,
    DhHostFoundCallback cb, void* user) {
    const auto found = ScanForHosts(port, timeoutMs ? timeoutMs : 1200);
    if (cb) {
        for (const auto& m : found) {
            cb(m.hostId, m.name.c_str(), m.address.c_str(), m.rttMs, m.sourceCount,
                m.acceptsInput ? 1 : 0, m.busy ? 1 : 0, user);
        }
    }
    return int(found.size());
}

DH_API int DH_CALL dh_api_version(void) {
    // 2 (GĐ9): thêm dh_list_displays, dh_discover_scan, số liệu có cấu trúc trong
    // DhAgentRow, và trường shareClipboard trong DhAgentOptions.
    // 3 (GĐ6 phía client): thêm dh_client_list_sources — client hỏi được host đang
    // chia sẻ những nguồn nào, thay vì luôn xem nguồn 0.
    // 4: DhAgentRow thêm hwnd/monitor (khớp dòng theo handle thay vì tên),
    // dh_agent_start thêm stoppedCb (báo phiên host tự chết), thêm
    // dh_client_mouse_move_rel (khoá chuột gửi delta) + dh_client_input_accepted.
    return 4;
}

DH_API int DH_CALL dh_is_elevated(void) {
    return IsProcessElevated() ? 1 : 0;
}

DH_API int DH_CALL dh_host_firewall_rule_present(void) {
    return HostFirewallRulePresent() ? 1 : 0;
}
