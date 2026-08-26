#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DH_SOURCE_QUERY_FAILED (-1)

typedef enum {
    DHAutoShareKeepWaiting = 0,
    DHAutoShareShareNow = 1,
    DHAutoShareGiveUpWaiting = 2,
} DHAutoShareStep;

typedef enum {
    DHPhaseIdle = 0,
    DHPhaseConnecting = 1,
    DHPhaseStreaming = 2,
    DHPhaseEnded = 3,
} DHPhase;

typedef enum {
    DHLinkUnknown = 0,
    DHLinkGood = 1,
    DHLinkFair = 2,
    DHLinkPoor = 3,
} DHLinkQuality;

typedef struct {
    bool haveRtt;
    uint32_t rttMs;
    uint8_t lossPct;
    DHLinkQuality quality;
} DHLinkHealth;

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
    bool acceptsInput;
    bool terminal;
    bool audio;
    bool files;
} DHHostCaps;

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
    DHStrConnectPromptTitle = 41,
    DHStrPickDisplaysHint = 42,
    DHStrNoDisplayTicked = 43,
    DHStrStopSelectedDisplay = 44,
    DHStrDisconnectSelectedViewer = 45,
    DHStrShareClampWarning = 46,
    DHStrShareStateOn = 47,
    DHStrShareStateOff = 48,
    DHStrStartSharing = 49,
    DHStrStartingShare = 50,
    DHStrRefreshNow = 51,
    DHStrStopDisplayAction = 52,
    DHStrDisconnectViewerAction = 53,
    DHStrLanDevicesHint = 54,
    DHStrNotSharing = 55,
    DHStrLanDevicesEmpty = 56,
    DHStrClientSettingsHeading = 57,
    DHStrClientSettingsHint = 58,
    DHStrUdpPortLabel = 59,
    DHStrBroadcastMemoryLabel = 60,
    DHStrRunInBackgroundLabel = 61,
    DHStrBackgroundPromptTitle = 62,
    DHStrBackgroundPromptMessage = 63,
    DHStrBackgroundPromptYes = 64,
    DHStrBackgroundPromptNo = 65,
    DHStrBackgroundPromptConfirm = 66,
    DHStrBackgroundPromptClose = 67,
    DHStrTrayRestore = 68,
    DHStrTrayExit = 69,
    DHStrAlreadyRunning = 70,
    DHStrBackgroundRunningHint = 71,
    DHStrQuitWhileBusyMessage = 72,
    DHStrQuitWhileBusyQuit = 73,
    DHStrQuitWhileBusyCancel = 74,
    DHStrHideTrayIconLabel = 75,
    DHStrBindInterfaceLabel = 76,
    DHStrBindAllInterfaces = 77,
    DHStrAutostartLabel = 78,
    DHStrAutoShareLabel = 79,
    DHStrClipboardSyncLabel = 80,
    DHStrKeepAwakeLabel = 124,
    DHStrEncryptSessionLabel = 106,
    DHStrEncryptSessionHint = 107,
    DHStrSessionKeyLabel = 108,
    DHStrSessionKeyHint = 109,
    DHStrCopySessionKey = 110,
    DHStrRefreshSessionKey = 111,
    DHStrEscrowSessionKeyLabel = 112,
    DHStrEscrowSessionKeyHint = 113,
    DHStrSessionKeyLifetimeLabel = 114,
    DHStrSessionKeyLifetimePerShare = 115,
    DHStrSessionKeyLifetimePersistent = 116,
    DHStrClientSessionKeyPrompt = 117,
    DHStrClientSessionKeyHint = 118,
    DHStrSessionKeyInvalid = 119,
    DHStrCloseToTrayLabel = 81,
    DHStrTrayShowWindow = 82,
    DHStrTrayHideWindow = 83,
    DHStrTrayQuit = 84,
    DHStrBindNotConnectedNote = 85,
    DHStrSettingsSectionVideo = 86,
    DHStrSettingsSectionConnection = 87,
    DHStrSettingsSectionSecurity = 88,
    DHStrSettingsSectionSession = 89,
    DHStrSettingsSectionLaunch = 90,
    DHStrLogMaxFileMbLabel = 91,
    DHStrLogCompressAfterDaysLabel = 92,
    DHStrLogDeleteAfterDaysLabel = 93,
    DHStrLogDetailsLabel = 94,
    DHStrLogRefresh = 95,
    DHStrLogOpenFolder = 96,
    DHStrLogEmpty = 97,
    DHStrLogDirLabel = 98,
    DHStrLogDirHint = 99,
    DHStrLogDirInvalid = 100,
    DHStrLogDirBrowse = 101,
    DHStrShareOnLaunchLabel = 102,
    DHStrLanguageLabel = 103,
    DHStrLanguageSystem = 104,
    DHStrSettingsSectionLanguage = 105,
    DHStrLanguageRestartHint = 120,
    DHStrForgetDevice = 121,
    DHStrCopied = 122,
    DHStrCopy = 123,
    DHStrWaitingForDisplays = 125,
    DHStrNoDisplayFound = 126,
    DHStrShareAudioLabel = 127,
    DHStrPlayAudioLabel = 128,
    DHStrAcceptFilesLabel = 129,
    DHStrSendFilesLabel = 130,
    DHStrDisconnectButton = 131,
    DHStrLinkReattaching = 132,
    DHStrOpenDesktopLabel = 133,
    DHStrOpenFilesLabel = 134,
    DHStrConnectedPickSession = 135,
    DHStrOpenShellLabel = 136,
    DHStrShareTerminalLabel = 137,
    DHStrTerminalCloseButton = 138,
    DHStrPairedHeading = 139,
    DHStrPairedHint = 140,
    DHStrPairedEmpty = 141,
    DHStrPairedForget = 142,
    DHStrPairedForgetAll = 143,
    DHStrPairedForgetAllPrompt = 144,
    DHStrPairedForgetNote = 145,
    DHStrAllowPairingLabel = 146,
    DHStrAllowPairingHint = 147,
    DHStrThisMachineHeading = 148,
    DHStrThisMachineHint = 149,
    DHStrPairedColumnName = 150,
    DHStrPairedColumnKey = 151,
    DHStrPairedColumnPaired = 152,
    DHStrPairedColumnLastSeen = 153,
    DHStrPairingRequestTitle = 154,
    DHStrPairingAllow = 155,
    DHStrPairingDeny = 156,
    DHStrFpsLabel = 157,
    DHStrBitrateMbpsLabel = 158,
    DHStrQualityLabel = 159,
    DHStrQualityNativeLabel = 160,
    DHStrOpenSystemSettingsLabel = 161,
} DHStringId;

const char* dh_string(DHStringId id);

bool dh_native_key_to_vk(int32_t native_key_code, int32_t* out_vk, int32_t* out_scan);

DHModifier dh_modifier_class(int32_t vk);

int32_t dh_vk_scancode(int32_t vk);

bool dh_is_lock_toggle_vk(int32_t vk);

bool dh_is_escape_vk(int32_t vk);

bool dh_parse_address(const char* address);

int dh_list_sources(const char* address, DHSourceInfo* out, int capacity, const char* passcode,
    DHHostCaps* out_caps);

bool dh_is_valid_passcode(const char* passcode);

int dh_passcode_digits(void);

int dh_max_sources(void);

uint32_t dh_auto_share_probe_ms(void);

DHAutoShareStep dh_auto_share_step(bool displays_ready, uint32_t waited_ms);

bool dh_connect_decision(const DHSourceInfo* sources, int count, uint8_t* out_source_id);

int dh_connecting_to(const char* address, char* out, int capacity);

int dh_could_not_connect(const char* address, char* out, int capacity);

int dh_source_query_failed(const char* address, char* out, int capacity);

int dh_source_query_empty(const char* address, char* out, int capacity);

int dh_udp_port_line(uint32_t port, char* out, int capacity);

int dh_compose_address(const char* host, const char* portText, char* out, int capacity);

int dh_address_host(const char* address, char* out, int capacity);

uint32_t dh_address_port(const char* address);

int dh_host_title(const char* address, uint32_t width, uint32_t height, char* out, int capacity);

int dh_zoom_label(double zoom, char* out, int capacity);

bool dh_is_zoomed(double zoom);

bool dh_should_refit_viewer(uint32_t fitted_w, uint32_t fitted_h, uint32_t new_w,
    uint32_t new_h);

void dh_fit_viewer_window(uint32_t video_w, uint32_t video_h, uint32_t work_w, uint32_t work_h,
    uint32_t* out_w, uint32_t* out_h);

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

void dh_set_data_dir(const char* dir);

void dh_viewer_opened(void);

bool dh_viewer_closed(void);
bool dh_viewers_open(void);

const char* dh_link_quality_text(DHLinkQuality quality);

int dh_link_ping_text(bool haveRtt, uint32_t rttMs, char* out, int capacity);

#ifdef __cplusplus
}
#endif
