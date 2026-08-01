#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdint>

#include "deskhub/media/CaptureContract.h"

using FrameInfo = deskhub::media::CapturedFrame<ID3D11Texture2D*>;
