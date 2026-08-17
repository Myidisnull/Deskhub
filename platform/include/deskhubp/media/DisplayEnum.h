#pragma once
#include "deskhub/media/ShareSource.h"

#include <cstdint>
#include <string>
#include <vector>

namespace deskhubp {

inline constexpr const char* kListDisplaysCancelled = "cancelled by the user";

std::vector<deskhub::media::ShareSource> ListDisplays();

std::string ListDisplaysError();

void ReleaseDisplays();

void SetLocalDisplay(uint32_t width, uint32_t height, const std::string& name);

}
