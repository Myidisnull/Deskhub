#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DH_ADDR_CAP 64
#define DH_PASSCODE_CAP 8
#define DH_SESSION_KEY_CAP 65
#define DH_LOG_NAME_CAP 96
#define DH_LOG_PATH_CAP 512
#define DH_LOG_DIR_CAP 1024

typedef struct {
    char addr[DH_ADDR_CAP];
    uint32_t rttMs;
} DHScanHit;

typedef struct {
    char name[DH_LOG_NAME_CAP];
    char path[DH_LOG_PATH_CAP];
    uint64_t sizeBytes;
} DHLogFile;

typedef struct {
    uint32_t probed;
    uint32_t total;
    uint32_t found;
    bool running;
} DHScanState;

typedef struct {
    char addr[DH_ADDR_CAP];
    char passcode[DH_PASSCODE_CAP];
    char status[16];
    char ping[16];
    char lastConnected[24];
    bool online;
} DHRecentRow;

typedef struct {
    uint32_t fps;
    uint32_t bitrateMbps;
    uint32_t maxDim;
    uint32_t port;
    bool allowInput;
    bool clientControl;
    bool runInBackground;
    bool runInBackgroundChoiceMade;
    bool hideTrayIcon;
    bool shareOnLaunch;
    uint32_t logMaxFileMb;
    uint32_t logCompressAfterDays;
    uint32_t logDeleteAfterDays;
    char logDir[DH_LOG_DIR_CAP];
    char passcode[DH_PASSCODE_CAP];
} DHUiSettings;

bool dh_scan_start(uint16_t port);
bool dh_scan_restart(uint16_t port);
void dh_scan_cancel(void);
DHScanState dh_scan_state(void);
int dh_scan_hits(DHScanHit* out, int capacity);
uint32_t dh_scan_rescan_secs(void);

void dh_recent_touch(const char* address, const char* passcode);
void dh_recent_touch_ex(const char* address, const char* passcode, bool encrypted,
    const char* session_key);
void dh_recent_remove(const char* address);
int dh_recent_passcode(const char* address, char* out, int capacity);
int dh_recent_session_key(const char* address, char* out, int capacity);
bool dh_recent_encrypted(const char* address);

void dh_status_stop(void);
void dh_status_refresh_now(void);

DHUiSettings dh_settings_load(void);
void dh_settings_save(uint32_t fps, uint32_t bitrate_mbps, uint32_t max_dim, uint32_t port,
    bool allow_input, bool client_control, bool run_in_background,
    bool run_in_background_choice_made, bool hide_tray_icon, bool share_on_launch,
    uint32_t log_max_file_mb, uint32_t log_compress_after_days, uint32_t log_delete_after_days,
    const char* log_dir, const char* passcode);

int dh_log_files(DHLogFile* out, int capacity);
int dh_log_read(const char* path, char* out, int capacity);
bool dh_log_open_folder(void);
bool dh_log_dir_usable(const char* path);
int dh_default_log_dir(char* out, int capacity);
bool dh_log_start_process(void);

int dh_recent_rows(DHRecentRow* out, int capacity);
void dh_status_watch_recent(void);
int dh_scan_status_text(uint16_t port, char* out, int capacity);
int dh_recent_note(char* out, int capacity);
int dh_ping_text(uint32_t rttMs, char* out, int capacity);
uint16_t dh_default_port(void);

bool dh_client_control(void);
void dh_set_client_control(bool on);

int dh_device_name(char* out, int capacity);
void dh_set_device_name(const char* name);

int dh_bind_ip(char* out, int capacity);
void dh_set_bind_ip(const char* ip);

bool dh_autostart_enabled(void);
void dh_set_autostart(bool on);

bool dh_auto_share(void);
void dh_set_auto_share(bool on);

bool dh_clipboard_sync(void);
void dh_set_clipboard_sync(bool on);

bool dh_share_audio(void);
void dh_set_share_audio(bool on);

bool dh_play_audio(void);
void dh_set_play_audio(bool on);

bool dh_accept_files(void);
void dh_set_accept_files(bool on);

bool dh_share_terminal(void);
void dh_set_share_terminal(bool on);

typedef struct {
    char name[80];
    char shortKey[16];
    char fingerprint[64];
    int64_t pairedUnix;
    int64_t lastSeenUnix;
} DHPairedDevice;

int dh_paired_devices(DHPairedDevice* out, int capacity);
bool dh_paired_forget(const char* fingerprint);
void dh_paired_forget_all(void);
bool dh_allow_pairing(void);
void dh_set_allow_pairing(bool allow);
int dh_own_fingerprint(char* out, int capacity);
int dh_format_address(uint64_t addr_packed, char* out, int capacity);
int dh_pairing_request_body(const char* name, const char* address, const char* short_key,
    char* out, int capacity);

bool dh_keep_awake(void);
void dh_set_keep_awake(bool on);

bool dh_encrypt_session(void);
void dh_set_encrypt_session(bool on);

bool dh_escrow_session_key(void);
void dh_set_escrow_session_key(bool on);

int dh_session_key_lifetime(void);
void dh_set_session_key_lifetime(int lifetime);

int dh_session_key_hex(char* out, int capacity);
bool dh_ensure_session_key(bool refresh);
bool dh_is_valid_session_key(const char* hex);

int dh_language(char* out, int capacity);
void dh_set_language(const char* code);
int dh_system_language(char* out, int capacity);

int dh_version_line(char* out, int capacity);
const char* dh_local_addresses(void);
int dh_idle_host_status(uint16_t port, char* out, int capacity);
int dh_sharing_status(uint16_t port, const char* passcode, bool allow_input, char* out,
    int capacity);

#ifdef __cplusplus
}
#endif
