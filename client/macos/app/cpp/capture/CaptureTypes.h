#pragma once
#include <cstdint>

#include "deskhub/media/CaptureContract.h"

struct MacFrameInfo {
    void* pixelBuffer = nullptr;
    deskhub::media::FrameMeta meta;
};

static_assert(deskhub::media::CapturedFrameLike<MacFrameInfo>,
    "MacFrameInfo must carry the shared frame metadata");
