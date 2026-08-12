#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "deskhubp/media/DisplayEnum.h"

#include <algorithm>
#include <string>
#include <utility>

namespace deskhubp {

namespace {

using deskhub::media::ShareSource;

struct Candidate {
    bool primary = false;
    ShareSource source;
};

BOOL CALLBACK EnumProc(HMONITOR mon, HDC, LPRECT, LPARAM lparam) {
    auto* out = reinterpret_cast<std::vector<Candidate>*>(lparam);

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return TRUE;

    Candidate c;
    c.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    c.source.targetId = uint64_t(uintptr_t(mon));
    c.source.x = int32_t(mi.rcMonitor.left);
    c.source.y = int32_t(mi.rcMonitor.top);
    c.source.width = uint32_t(mi.rcMonitor.right - mi.rcMonitor.left);
    c.source.height = uint32_t(mi.rcMonitor.bottom - mi.rcMonitor.top);
    c.source.name = "Display " + std::to_string(out->size() + 1);
    if (c.primary) c.source.name += " (primary)";
    out->push_back(std::move(c));
    return TRUE;
}

}

std::vector<deskhub::media::ShareSource> ListDisplays() {
    std::vector<Candidate> found;
    EnumDisplayMonitors(nullptr, nullptr, EnumProc, reinterpret_cast<LPARAM>(&found));
    std::stable_partition(found.begin(), found.end(),
        [](const Candidate& c) { return c.primary; });

    std::vector<ShareSource> out;
    out.reserve(found.size());
    for (Candidate& c : found) out.push_back(std::move(c.source));
    return out;
}

std::string ListDisplaysError() {
    return {};
}

void ReleaseDisplays() {}

void SetLocalDisplay(uint32_t, uint32_t, const std::string&) {}

}
