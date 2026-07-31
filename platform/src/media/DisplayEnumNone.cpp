#include "deskhubp/media/DisplayEnum.h"

namespace deskhubp {

std::vector<deskhub::media::ShareSource> ListDisplays() {
    return {};
}

std::string ListDisplaysError() {
    return "screen sharing is not supported on this platform";
}

void ReleaseDisplays() {}

}
