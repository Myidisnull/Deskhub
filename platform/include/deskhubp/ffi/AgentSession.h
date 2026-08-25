#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    char name[256];
} DHShareSource;

typedef struct {
    bool viewer;
    uint8_t sourceId;
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

bool dha_start(const DHShareSource* sources, int count, uint32_t fps, uint32_t bitrate_mbps,
    uint32_t max_dim, uint16_t port, bool allow_input, const char* passcode);

bool dha_start_files(uint16_t port, const char* passcode);

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

void dha_offer_audio(const int16_t* pcm, int samples);
bool dha_audio_running(void);

typedef struct {
    uint64_t addrPacked;
    char shortKey[16];
    char name[80];
} DHPairingRequest;

int dh_share_take_pairing_requests(DHPairingRequest* out, int capacity);
void dh_share_answer_pairing(uint64_t addr_packed, bool allowed);

#ifdef __cplusplus
}
#endif
