#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace deskhub::media {

inline std::string SourceSizeLabel(uint32_t width, uint32_t height) {
    return std::to_string(width) + "x" + std::to_string(height);
}

inline std::string SourceName(std::string_view name, uint8_t sourceId) {
    if (!name.empty()) return std::string(name);
    return "Source " + std::to_string(unsigned(sourceId));
}

inline std::string SourcePickerLabel(std::string_view name, uint8_t sourceId, uint32_t width,
    uint32_t height) {
    return SourceName(name, sourceId) + " (" + SourceSizeLabel(width, height) + ")";
}

inline std::string SharedSourceLabel(std::string_view name, uint32_t width, uint32_t height,
    bool viewerConnected) {
    std::string label(name);
    label += "  (";
    label += SourceSizeLabel(width, height);
    if (viewerConnected) label += ", viewer connected";
    label += ")";
    return label;
}

}
