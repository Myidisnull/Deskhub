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
    uint8_t sourceId;
    uint32_t width;
    uint32_t height;
    bool viewerConnected;
    bool zeroCopy;
    double captureFps;
    double sendFps;
    double sendKbps;
    uint32_t rttMs;
    char viewerAddr[64];
    char name[256];
    char label[320];
} DHAgentStatus;

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
    uint32_t max_dim);

void dha_stop(void);

bool dha_running(void);

int dha_status(DHAgentStatus* out, int capacity);

const char* dha_last_error(void);

const char* dha_local_addresses(void);

#ifdef __cplusplus
}
#endif
