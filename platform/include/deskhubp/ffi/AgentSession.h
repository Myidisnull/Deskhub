#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "deskhubp/ffi/TerminalFfi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DHShellLive = 0,
    DHShellDetached = 1,
    DHShellLocal = 2,
} DHShellState;

typedef struct {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    char name[256];
} DHShareSource;

typedef struct {
    bool viewer;
    bool terminal;
    uint8_t sourceId;
    uint32_t termId;
    uint8_t shellState;
    bool online;
    char viewerAddr[192];
    char source[256];
    char size[32];
    char viewers[16];
    char client[192];
    char capture[16];
    char send[16];
    char mbps[16];
    char rtt[32];
} DHHostRow;

typedef struct {
    uint32_t fps;
    uint32_t bitrateMbps;
    uint32_t maxDim;
} DHShareDefaults;

typedef struct {
    char label[16];
    uint32_t maxDim;
} DHQualityPreset;

DHShareDefaults dha_default_options(void);

int dha_quality_presets(DHQualityPreset* out, int capacity);

int dha_list_share_sources(DHShareSource* out, int capacity);

typedef struct {
    uint64_t addrPacked;
    char shortKey[16];
    char name[80];
} DHPairingRequest;

bool dha_start(const DHShareSource* sources, int count, uint32_t fps, uint32_t bitrate_mbps,
    uint32_t max_dim, uint16_t port, bool allow_input, const char* passcode, bool terminal);

bool dha_terminal_active(void);

void dha_kick_shell(uint32_t term_id);

void dha_stop_terminal(void);

bool dha_attach_shell(uint32_t term_id);

bool dha_local_shell_alive(uint32_t term_id);

void dha_close_local_shell(uint32_t term_id);

bool dha_local_grid(uint32_t term_id, uint32_t scrollOffset, DHTermCell* cells,
    uint32_t cellCapacity, DHTermGrid* outGrid);

void dha_local_send_key(uint32_t term_id, int32_t key, uint32_t codepoint, bool shift, bool alt,
    bool ctrl);

void dha_local_send_text(uint32_t term_id, const char* utf8);

void dha_local_resize(uint32_t term_id, uint16_t cols, uint16_t rows);

int dha_take_pairing_requests(DHPairingRequest* out, int capacity);

void dha_answer_pairing(uint64_t addr_packed, bool allowed);

void dha_stop(void);

void dha_stop_source(uint8_t source_id);

void dha_kick_viewer(uint8_t source_id, const char* viewer_addr);

bool dha_running(void);

int dha_host_rows(DHHostRow* out, int capacity);

const char* dha_last_error(void);

const char* dha_local_addresses(void);

int dha_bind_warning(char* out, int capacity);

void dha_clip_offer(const char* text);
int dha_clip_take(char* out, int capacity);

#ifdef __cplusplus
}
#endif
