#pragma once
#include <cstdint>
#include <vector>

#include "deskhub/media/CaptureContract.h"

enum class FrameMemory : uint8_t {
    Mapped,
    DmaBuf,
};

inline constexpr uint32_t kMaxDmaPlanes = 4;

struct DmaPlane {
    int fd = -1;
    uint32_t offset = 0;
    uint32_t stride = 0;
};

struct LinuxFrameInfo : deskhub::media::CapturedFrame<const uint8_t*> {
    FrameMemory memory = FrameMemory::Mapped;

    uint32_t stride = 0;

    DmaPlane planes[kMaxDmaPlanes]{};
    uint32_t planeCount = 0;
    uint64_t modifier = 0;

    uint32_t drmFormat = 0;
};

struct CopiedFrame {
    std::vector<uint8_t> pixels;
    uint32_t stride = 0;
    uint32_t drmFormat = 0;
    deskhub::media::FrameMeta meta;
};

inline LinuxFrameInfo FrameFromCopy(const CopiedFrame& copy) {
    LinuxFrameInfo fi;
    fi.memory = FrameMemory::Mapped;
    fi.handle = copy.pixels.data();
    fi.stride = copy.stride;
    fi.drmFormat = copy.drmFormat;
    fi.meta = copy.meta;
    return fi;
}
