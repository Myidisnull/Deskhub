#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DH_ADDR_CAP 64
#define DH_PASSCODE_CAP 8

typedef struct {
    char addr[DH_ADDR_CAP];
    uint32_t rttMs;
} DHScanHit;

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
    char passcode[DH_PASSCODE_CAP];
} DHUiSettings;

bool dh_scan_start(uint16_t port);
bool dh_scan_restart(uint16_t port);
void dh_scan_cancel(void);
DHScanState dh_scan_state(void);
int dh_scan_hits(DHScanHit* out, int capacity);
uint32_t dh_scan_rescan_secs(void);

void dh_recent_touch(const char* address, const char* passcode);
void dh_recent_remove(const char* address);
int dh_recent_passcode(const char* address, char* out, int capacity);

void dh_status_stop(void);
void dh_status_refresh_now(void);

DHUiSettings dh_settings_load(void);
void dh_settings_save(uint32_t fps, uint32_t bitrate_mbps, uint32_t max_dim, uint32_t port,
    bool allow_input, bool client_control, const char* passcode);

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

bool dh_start_hidden(void);
void dh_set_start_hidden(bool on);

int dh_version_line(char* out, int capacity);
const char* dh_local_addresses(void);
int dh_idle_host_status(uint16_t port, char* out, int capacity);
int dh_sharing_status(uint16_t port, const char* passcode, bool allow_input, char* out,
    int capacity);

#ifdef __cplusplus
}
#endif
