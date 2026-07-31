#pragma once
#include <stdint.h>

#if defined(_WIN32)
#define DH_API extern "C" __declspec(dllexport)
#define DH_CALL __stdcall
#else
#define DH_API extern "C"
#define DH_CALL
#endif

struct DhClientHandle;

typedef void(DH_CALL* DhClientStatsCallback)(const char* statsUtf8, void* user);
typedef void(DH_CALL* DhClientSizeCallback)(uint32_t width, uint32_t height, void* user);
typedef void(DH_CALL* DhClientClosedCallback)(const char* reasonUtf8, void* user);

DH_API DhClientHandle* DH_CALL dh_client_start_hwnd(const char* addrUtf8, uint8_t sourceId,
    uint64_t hwnd, DhClientStatsCallback statsCb, DhClientSizeCallback sizeCb,
    DhClientClosedCallback closedCb, void* user);

DH_API void DH_CALL dh_client_mouse_move(DhClientHandle* h, uint16_t nx, uint16_t ny);
DH_API void DH_CALL dh_client_mouse_move_rel(DhClientHandle* h, int dx, int dy);
DH_API void DH_CALL dh_client_mouse_button(DhClientHandle* h, int button, int down);
DH_API void DH_CALL dh_client_wheel(DhClientHandle* h, int delta);
DH_API void DH_CALL dh_client_key(DhClientHandle* h, int vk, int scan, int down);

DH_API void DH_CALL dh_client_stop(DhClientHandle* h);
