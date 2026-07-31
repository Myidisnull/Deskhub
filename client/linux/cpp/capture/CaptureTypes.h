#pragma once
#include <cstdint>

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

struct LinuxFrameInfo {
    FrameMemory memory = FrameMemory::Mapped;

    const uint8_t* data = nullptr;
    uint32_t stride = 0;

    DmaPlane planes[kMaxDmaPlanes]{};
    uint32_t planeCount = 0;
    uint64_t modifier = 0;

    uint32_t drmFormat = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t timestampUs = 0;
};
