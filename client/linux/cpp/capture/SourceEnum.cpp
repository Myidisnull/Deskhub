// =============================================================================
// SourceEnum.cpp — lớp mỏng trên PortalScreenCast.
//
// Toàn bộ việc thật nằm ở capture/PortalScreenCast.cpp; file này chỉ đổi kiểu dữ
// liệu portal sang kiểu mà UI và AgentLoop dùng, để hai bên đó không phải include
// header GDBus. Nó tồn tại vì hai lý do:
//   1. Giữ ĐÚNG TÊN HÀM với bản macOS (GetShareSources) — code hai nền tảng đọc
//      giống nhau, dù cơ chế bên dưới khác hẳn.
//   2. Ranh giới để sau này thêm backend X11 (XRandR liệt kê màn hình, XShm bắt
//      hình) mà không đụng vào UI: chỉ cần rẽ nhánh theo XDG_SESSION_TYPE ở đây.
//
// LIÊN QUAN: capture/SourceEnum.h (⚠ vì sao hàm này NGƯỢC hướng so với Windows/macOS)
// =============================================================================
#include "capture/SourceEnum.h"

#include "capture/PortalScreenCast.h"

std::vector<ShareSource> GetShareSources() {
    std::vector<ShareSource> out;

    PortalScreenCast& portal = PortalScreenCast::Instance();
    if (!portal.Open()) return out;

    for (const PortalStream& s : portal.streams()) {
        ShareSource ss;
        ss.nodeId = s.nodeId;
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
