#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdint>

#include "deskhub/media/CaptureContract.h"

struct FrameInfo {
    ID3D11Texture2D* texture = nullptr;
    deskhub::media::FrameMeta meta;
};

static_assert(deskhub::media::CapturedFrameLike<FrameInfo>,
    "FrameInfo must carry the shared frame metadata");
