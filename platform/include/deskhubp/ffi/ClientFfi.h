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
    char displayName[256];
    char sizeLabel[32];
    char pickerLabel[320];
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
    double x;
    double y;
} DHCursor;

typedef struct {
    char label[16];
    int32_t vk;
    int32_t scan;
    int32_t modVk;
    int32_t modScan;
} DHHotkey;

typedef enum {
    DHModifierNone = 0,
    DHModifierShift = 1,
    DHModifierControl = 2,
    DHModifierOption = 3,
    DHModifierCommand = 4,
    DHModifierCapsLock = 5,
} DHModifier;

typedef struct {
    bool locked;
} DHPointerLock;

typedef struct {
    bool lockChanged;
    bool releaseHeldInput;
} DHPointerLockEffect;

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
    DHStrSessionEnded = 18,
    DHStrShareStartFailed = 19,
    DHStrScreenRecordingRequired = 20,
    DHStrClientPasscodePrompt = 21,
    DHStrClientPasscodeHint = 22,
    DHStrPasscodeInvalid = 23,
    DHStrPasscodeLabel = 24,
    DHStrLanDevicesHeading = 25,
    DHStrRecentDevicesHeading = 26,
    DHStrRecentDevicesHint = 27,
    DHStrRecentDevicesEmpty = 28,
    DHStrSidebarHost = 29,
    DHStrSidebarClient = 30,
    DHStrSidebarSettings = 31,
    DHStrHostHeading = 32,
    DHStrClientHeading = 33,
    DHStrSettingsHeading = 34,
    DHStrSettingsHint = 35,
    DHStrProjectUrl = 36,
    DHStrProjectLinkLabel = 37,
    DHStrAllowControlLabel = 38,
    DHStrRequestControlLabel = 39,
    DHStrClientIpPlaceholder = 40,
} DHStringId;

const char* dh_string(DHStringId id);

bool dh_native_key_to_vk(int32_t native_key_code, int32_t* out_vk, int32_t* out_scan);

DHModifier dh_modifier_class(int32_t vk);

int32_t dh_vk_scancode(int32_t vk);

bool dh_is_lock_toggle_vk(int32_t vk);

bool dh_is_escape_vk(int32_t vk);

bool dh_parse_address(const char* address);

int dh_list_sources(const char* address, DHSourceInfo* out, int capacity, const char* passcode);

bool dh_is_valid_passcode(const char* passcode);

int dh_passcode_digits(void);

bool dh_connect_decision(const DHSourceInfo* sources, int count, uint8_t* out_source_id);

int dh_connecting_to(const char* address, char* out, int capacity);

int dh_could_not_connect(const char* address, char* out, int capacity);

int dh_host_title(const char* address, uint32_t width, uint32_t height, char* out, int capacity);

int dh_zoom_label(double zoom, char* out, int capacity);

bool dh_is_zoomed(double zoom);

int dh_viewer_base_title(const char* sourceName, char* out, int capacity);

int dh_hotkeys(DHHotkey* out, int capacity);

DHViewRect dh_video_rect(double viewportW, double viewportH, double aspect, DHViewTransform t);

DHViewTransform dh_apply_gesture(DHViewTransform cur, double factor, double centroidX,
    double centroidY, double panDeltaX, double panDeltaY, double viewportW, double viewportH,
    double aspect);

bool dh_normalize_pointer(double px, double py, DHViewRect rect, int32_t* nx, int32_t* ny);

int32_t dh_take_scroll_notches(double dragPoints, double* carry);

int32_t dh_scroll_notches_from_lines(double lines);

DHCursor dh_cursor_clamp(DHCursor cur, DHViewRect video, double viewportW, double viewportH);

DHCursor dh_cursor_move(DHCursor cur, double dx, double dy, DHViewRect video, double viewportW,
    double viewportH);

bool dh_cursor_point(DHCursor cur, DHViewRect video, double* px, double* py);

bool dh_cursor_normalize(DHCursor cur, DHViewRect video, int32_t* nx, int32_t* ny);

DHPointerLockEffect dh_pointer_toggle_lock(DHPointerLock* state);

DHPointerLockEffect dh_pointer_escape(DHPointerLock* state);

DHPointerLockEffect dh_pointer_focus_lost(DHPointerLock* state);

int dh_pointer_subtitle(DHPointerLock state, const char* statusLine, char* out, int capacity);

void dh_viewer_opened(void);

bool dh_viewer_closed(void);

#ifdef __cplusplus
}
#endif
