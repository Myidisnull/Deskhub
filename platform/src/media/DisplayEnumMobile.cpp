#include "deskhubp/media/DisplayEnum.h"

#include <mutex>

namespace deskhubp {
namespace {

constexpr uint64_t kLocalDisplayId = 1;

std::mutex g_displayMutex;
deskhub::media::ShareSource g_display;

}

std::vector<deskhub::media::ShareSource> ListDisplays() {
    std::lock_guard<std::mutex> lk(g_displayMutex);
    if (!g_display.width || !g_display.height) return {};
    return {g_display};
}

std::string ListDisplaysError() {
    std::lock_guard<std::mutex> lk(g_displayMutex);
    if (g_display.width && g_display.height) return {};
    return "this device has not reported its screen size yet";
}

void ReleaseDisplays() {}

void ForgetDisplaySelection() {}

void SetLocalDisplay(uint32_t width, uint32_t height, const std::string& name) {
    std::lock_guard<std::mutex> lk(g_displayMutex);
    g_display.targetId = kLocalDisplayId;
    g_display.width = width;
    g_display.height = height;
    g_display.name = name;
}

}
