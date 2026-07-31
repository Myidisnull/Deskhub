#include "capture/DisplayFinder.h"

#include <algorithm>

namespace {

BOOL CALLBACK EnumProc(HMONITOR mon, HDC, LPRECT, LPARAM lparam) {
    auto* out = reinterpret_cast<std::vector<DisplayInfo>*>(lparam);

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return TRUE;

    DisplayInfo d;
    d.monitor = mon;
    d.width = uint32_t(mi.rcMonitor.right - mi.rcMonitor.left);
    d.height = uint32_t(mi.rcMonitor.bottom - mi.rcMonitor.top);
    d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    d.name = L"Display " + std::to_wstring(out->size() + 1);
    if (d.primary) d.name += L" (primary)";
    out->push_back(std::move(d));
    return TRUE;
}

}

std::vector<DisplayInfo> ListDisplays() {
    std::vector<DisplayInfo> out;
    EnumDisplayMonitors(nullptr, nullptr, EnumProc, reinterpret_cast<LPARAM>(&out));
    std::stable_partition(out.begin(), out.end(),
        [](const DisplayInfo& d) { return d.primary; });
    return out;
}
