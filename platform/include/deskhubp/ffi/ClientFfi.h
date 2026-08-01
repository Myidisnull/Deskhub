#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DHPhaseIdle = 0,
    DHPhaseConnecting = 1,
    DHPhaseStreaming = 2,
    DHPhaseEnded = 3,
} DHPhase;

typedef struct {
    uint8_t sourceId;
    uint16_t width;
    uint16_t height;
    char name[256];
} DHSourceInfo;

typedef struct {
    double x;
    double y;
    double width;
    double height;
} DHViewRect;

typedef struct {
    double zoom;
    double panX;
    double panY;
} DHViewTransform;

typedef struct {
    char label[16];
    int32_t vk;
    int32_t scan;
    int32_t modVk;
    int32_t modScan;
} DHHotkey;

typedef enum {
    DHStrAppTitle = 0,
    DHStrHostIpIntro = 1,
    DHStrNoNetworkAddress = 2,
    DHStrClientIpPrompt = 3,
    DHStrPickerTitle = 4,
    DHStrPickerEachWindow = 5,
    DHStrShareButton = 6,
    DHStrSharingTitle = 7,
    DHStrSharingSourcesIntro = 8,
    DHStrSharingConnectHint = 9,
    DHStrNothingShared = 10,
    DHStrStopSharing = 11,
    DHStrQueryingSources = 12,
    DHStrViewerOpenFailed = 13,
    DHStrConnectionEndedTitle = 14,
    DHStrDisconnected = 15,
    DHStrUdpPortLine = 16,
    DHStrInvalidAddressHint = 17,
} DHStringId;

const char* dh_string(DHStringId id);

bool dh_parse_address(const char* address);

int dh_list_sources(const char* address, DHSourceInfo* out, int capacity);

int dh_source_picker_label(const char* name, uint8_t sourceId, uint16_t width, uint16_t height,
    char* out, int capacity);

int dh_shared_source_label(const char* name, uint16_t width, uint16_t height,
    bool viewerConnected, char* out, int capacity);

int dh_viewer_base_title(const char* sourceName, char* out, int capacity);

int dh_viewer_subtitle(const char* statusLine, bool mouseLocked, char* out, int capacity);

int dh_hotkeys(DHHotkey* out, int capacity);

DHViewRect dh_video_rect(double viewportW, double viewportH, double aspect, DHViewTransform t);

DHViewTransform dh_apply_gesture(DHViewTransform cur, double factor, double centroidX,
    double centroidY, double panDeltaX, double panDeltaY, double viewportW, double viewportH,
    double aspect);

bool dh_normalize_pointer(double px, double py, DHViewRect rect, int32_t* nx, int32_t* ny);

#ifdef __cplusplus
}
#endif
