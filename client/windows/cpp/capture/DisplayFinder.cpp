// =============================================================================
// DisplayFinder.cpp — cài đặt liệt kê màn hình.
//
// EnumDisplayMonitors gọi callback cho từng màn hình; GetMonitorInfo cho rect và cờ
// MONITORINFOF_PRIMARY. Cả hai đều rẻ và đồng bộ, không cần thread.
//
// DÙNG rcMonitor CHỨ KHÔNG PHẢI rcWork: rcWork trừ đi taskbar, mà ta capture cả màn
// hình kể cả taskbar — khai kích thước nhỏ hơn thật sẽ làm client dựng decoder sai cỡ.
//
// LIÊN QUAN: capture/DisplayFinder.h (vai trò + quy ước đặt tên)
// =============================================================================
#include "capture/DisplayFinder.h"

#include <algorithm>

namespace {

BOOL CALLBACK EnumProc(HMONITOR mon, HDC, LPRECT, LPARAM lparam) {
    auto* out = reinterpret_cast<std::vector<DisplayInfo>*>(lparam);

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return TRUE; // bỏ màn hình này, tiếp tục quét

    DisplayInfo d;
    d.monitor = mon;
    d.width = uint32_t(mi.rcMonitor.right - mi.rcMonitor.left);
    d.height = uint32_t(mi.rcMonitor.bottom - mi.rcMonitor.top);
    d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    // Đánh số theo thứ tự tìm thấy — khớp với cách Windows đánh số trong Settings
    // đủ gần để người dùng nhận ra, và không đòi đọc EDID.
    d.name = L"Display " + std::to_wstring(out->size() + 1);
    if (d.primary) d.name += L" (primary)";
    out->push_back(std::move(d));
    return TRUE;
}

} // namespace

std::vector<DisplayInfo> ListDisplays() {
    std::vector<DisplayInfo> out;
    EnumDisplayMonitors(nullptr, nullptr, EnumProc, reinterpret_cast<LPARAM>(&out));
    // Màn hình chính lên đầu. stable_partition giữ nguyên thứ tự tương đối của phần
    // còn lại, nên số hiệu trong tên vẫn khớp với thứ tự Windows liệt kê.
    std::stable_partition(out.begin(), out.end(),
        [](const DisplayInfo& d) { return d.primary; });
    return out;
}
