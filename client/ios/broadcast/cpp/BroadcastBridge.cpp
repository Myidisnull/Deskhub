#include "BroadcastBridge.h"

#include <CoreVideo/CVPixelBuffer.h>

#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "capture/ScreenCapture.h"

#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/ffi/AgentSession.h"
#include "deskhubp/ffi/DiscoveryFfi.h"
#include "deskhubp/ffi/FfiText.h"
#include "deskhubp/media/DisplayEnum.h"

namespace {

constexpr int kMaxViewerRows = 16;

enum class StartState { Idle,
    Starting,
    Sharing,
    Failed };

std::mutex g_startMutex;
StartState g_startState = StartState::Idle;
std::string g_startError;
std::thread g_startThread;

std::string DeviceScreenName() {
    return "iPhone screen";
}

std::string StartSharing(uint32_t width, uint32_t height) {
    deskhubp::SetLocalDisplay(width, height, DeviceScreenName());

    DHShareSource source{};
    if (dha_list_share_sources(&source, 1) != 1)
        return "The broadcast reported no screen size.";

    const DHUiSettings settings = dh_settings_load();
    const DHShareDefaults defaults = dha_default_options();
    const bool ok = dha_start(&source, 1, settings.fps ? settings.fps : defaults.fps,
        settings.bitrateMbps ? settings.bitrateMbps : defaults.bitrateMbps, settings.maxDim,
        uint16_t(settings.port), false, settings.passcode);
    if (ok) return std::string();

    const char* reason = dha_last_error();
    return reason && *reason ? std::string(reason) : std::string("Sharing could not start.");
}

void SpawnStart(uint32_t width, uint32_t height) {
    g_startState = StartState::Starting;
    g_startError.clear();
    g_startThread = std::thread([width, height] {
        std::string failure = StartSharing(width, height);
        if (!failure.empty())
            LOGE("[Broadcast] Could not start sharing: %s", failure.c_str());
        std::lock_guard<std::mutex> lk(g_startMutex);
        g_startState = failure.empty() ? StartState::Sharing : StartState::Failed;
        g_startError = std::move(failure);
    });
}

std::thread TakeStartThread() {
    std::lock_guard<std::mutex> lk(g_startMutex);
    return std::move(g_startThread);
}

}

void dhb_use_app_group(const char* containerPath) {
    deskhubp::SetAppDataDir(containerPath ? std::string(containerPath) : std::string());
}

void dhb_push_frame(void* pixelBuffer, uint64_t timestampUs) {
    if (!pixelBuffer) return;
    auto pb = static_cast<CVPixelBufferRef>(pixelBuffer);
    const uint32_t width = uint32_t(CVPixelBufferGetWidth(pb));
    const uint32_t height = uint32_t(CVPixelBufferGetHeight(pb));
    if (!width || !height) return;

    {
        std::lock_guard<std::mutex> lk(g_startMutex);
        if (g_startState == StartState::Idle) SpawnStart(width, height);
    }

    ScreenCapture::Frame frame;
    frame.handle = pixelBuffer;
    frame.meta.width = width;
    frame.meta.height = height;
    frame.meta.timestampUs = timestampUs;
    ScreenCapture::DeliverFrame(frame);
}

void dhb_finish_broadcast(void) {
    ScreenCapture::ReportBroadcastFinished();

    std::thread pendingStart = TakeStartThread();
    if (pendingStart.joinable()) pendingStart.join();

    dha_stop();

    std::lock_guard<std::mutex> lk(g_startMutex);
    g_startState = StartState::Idle;
    g_startError.clear();
}

bool dhb_sharing(void) {
    return dha_running();
}

int dhb_viewer_count(void) {
    std::vector<DHHostRow> rows(kMaxViewerRows);
    const int count = dha_host_rows(rows.data(), kMaxViewerRows);
    int viewers = 0;
    for (int i = 0; i < count; ++i)
        if (rows[size_t(i)].viewer) ++viewers;
    return viewers;
}

int dhb_last_error(char* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    std::lock_guard<std::mutex> lk(g_startMutex);
    deskhubp::CopyToBuf(out, size_t(capacity), g_startError);
    return int(std::strlen(out));
}
