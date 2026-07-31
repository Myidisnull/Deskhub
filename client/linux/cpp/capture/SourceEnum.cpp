#include "capture/SourceEnum.h"

#include "capture/PortalScreenCast.h"

std::vector<ShareSource> GetShareSources() {
    std::vector<ShareSource> out;

    PortalScreenCast& portal = PortalScreenCast::Instance();
    if (!portal.Open()) return out;

    for (const PortalStream& s : portal.streams()) {
        ShareSource ss;
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

std::string ShareSourceError() {
    return PortalScreenCast::Instance().lastError();
}

void ReleaseShareSources() {
    PortalScreenCast::Instance().Close();
}
