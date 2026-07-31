#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdint>

struct FrameInfo {
    ID3D11Texture2D* texture;
    uint32_t width;
    uint32_t height;
    uint64_t timestampUs;
    uint64_t frameId;
};
