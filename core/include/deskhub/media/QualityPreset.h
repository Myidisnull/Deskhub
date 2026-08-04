#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace deskhub::media {

struct QualityPreset {
    const char* label;
    uint32_t maxDim;
};

inline constexpr uint32_t kNativeMaxDim = 0;

inline constexpr std::array<QualityPreset, 4> kQualityPresets{{
    {"720p", 1280},
    {"1080p", 1920},
    {"1440p", 2560},
    {"Native", kNativeMaxDim},
}};

inline constexpr size_t QualityPresetIndex(uint32_t maxDim) {
    for (size_t i = 0; i < kQualityPresets.size(); ++i)
        if (kQualityPresets[i].maxDim == maxDim) return i;
    return 0;
}

inline constexpr uint32_t QualityPresetMaxDim(size_t index, uint32_t fallback) {
    return index < kQualityPresets.size() ? kQualityPresets[index].maxDim : fallback;
}

}
