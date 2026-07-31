#pragma once
#include <string>
#include <vector>

#include "deskhub/media/ShareSource.h"

using deskhub::media::ShareSource;

std::vector<ShareSource> GetShareSources();

std::string ShareSourceError();

void ReleaseShareSources();
