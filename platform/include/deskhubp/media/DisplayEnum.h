#pragma once
#include "deskhub/media/ShareSource.h"

#include <string>
#include <vector>

namespace deskhubp {

std::vector<deskhub::media::ShareSource> ListDisplays();

std::string ListDisplaysError();

void ReleaseDisplays();

}
