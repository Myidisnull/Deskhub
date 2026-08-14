#pragma once
#include "deskhub/media/ShareSource.h"

#include <cstdint>
#include <string>
#include <vector>

namespace deskhubp {

std::vector<deskhub::media::ShareSource> ListDisplays();

std::string ListDisplaysError();

void ReleaseDisplays();

void ForgetDisplaySelection();

void SetLocalDisplay(uint32_t width, uint32_t height, const std::string& name);

}
