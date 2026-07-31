#include "deskhubp/media/DisplayEnum.h"

#include <utility>

#include "capture/PortalScreenCast.h"

namespace deskhubp {

std::vector<deskhub::media::ShareSource> ListDisplays() {
    std::vector<deskhub::media::ShareSource> out;

    PortalScreenCast& portal = PortalScreenCast::Instance();
    if (!portal.Open()) return out;

    for (const PortalStream& s : portal.streams()) {
        deskhub::media::ShareSource ss;
        ss.targetId = s.nodeId;
        ss.name = s.name;
        ss.x = s.x;
        ss.y = s.y;
        ss.width = s.width;
        ss.height = s.height;
        out.push_back(std::move(ss));
    }
    return out;
}

std::string ListDisplaysError() {
    return PortalScreenCast::Instance().lastError();
}

void ReleaseDisplays() {
    PortalScreenCast::Instance().Close();
}

}
