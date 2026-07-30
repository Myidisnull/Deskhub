// =============================================================================
// VaDisplay.cpp — dò render node và vaInitialize.
//
// LIÊN QUAN: encode/VaDisplay.h (lý do thiết kế đầy đủ)
// =============================================================================
#include "encode/VaDisplay.h"

#include <va/va_drm.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "Log.h"

namespace {
std::once_flag g_once;

// libva in cảnh báo ra stderr qua callback riêng. Không nối vào LOG* vì chuỗi
// định dạng của nó khác; để mặc định là đủ — thông điệp vẫn tới cùng một stderr.

} // namespace

bool VaHasEntrypoint(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint) {
    const int maxEp = vaMaxNumEntrypoints(dpy);
    if (maxEp <= 0) return false;
    // static_cast chứ không size_t(...): `eps(size_t(maxEp))` bị trình dịch đọc
    // thành KHAI BÁO HÀM (most vexing parse), và lỗi báo ra ở tận dòng dưới.
    std::vector<VAEntrypoint> eps(static_cast<size_t>(maxEp));
    int n = 0;
    if (vaQueryConfigEntrypoints(dpy, profile, eps.data(), &n) != VA_STATUS_SUCCESS) return false;
    for (int i = 0; i < n; ++i)
        if (eps[size_t(i)] == entrypoint) return true;
    return false;
}

VaDisplay& VaDisplay::Instance() {
    static VaDisplay inst;
    return inst;
}

VaDisplay::~VaDisplay() {
    if (dpy_) vaTerminate(dpy_);
    if (drmFd_ >= 0) close(drmFd_);
}

bool VaDisplay::Open() {
    std::call_once(g_once, [this] {
        // Danh sách ứng viên: biến môi trường trước (đường ép tay), rồi quét
        // renderD128..143 — dải chuẩn của render node trên Linux.
        std::vector<std::string> candidates;
        if (const char* forced = std::getenv("DESKHUB_VA_DEVICE")) candidates.emplace_back(forced);
        for (int i = 128; i <= 143; ++i) {
            char p[64];
            std::snprintf(p, sizeof(p), "/dev/dri/renderD%d", i);
            candidates.emplace_back(p);
        }

        for (const std::string& path : candidates) {
            const int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
            if (fd < 0) continue;

            VADisplay dpy = vaGetDisplayDRM(fd);
            if (!dpy) {
                close(fd);
                continue;
            }
            int major = 0, minor = 0;
            if (vaInitialize(dpy, &major, &minor) != VA_STATUS_SUCCESS) {
                vaTerminate(dpy);
                close(fd);
                continue;
            }

            // Dò theo NĂNG LỰC, không lấy bừa node đầu tiên — xem VaDisplay.h.
            // Main đủ cho ta (VaEncoder chỉ phát bitstream Main); chấp nhận cả
            // High vì card nào làm được High thì chắc chắn làm được Main.
            //
            // HAI entrypoint mã hoá, KHÔNG PHẢI MỘT. EncSlice là đường VME cổ
            // điển; EncSliceLP là đường "low power" (VDEnc — khối mã hoá cứng
            // riêng). Chỉ hỏi EncSlice là bỏ rơi cả một lớp máy: Intel Gen11+
            // (Ice Lake, Tiger Lake, Rocket Lake — UHD 7xx) đã BỎ đường VME cho
            // H.264, driver iHD ở đó chỉ khai EncSliceLP. Máy như thế mã hoá phần
            // cứng tốt nhưng bị chuỗi dò cũ loại, rồi người dùng nhận đúng một
            // thông điệp sai: "đi cài driver đi" — trong khi driver không thiếu gì.
            VAEntrypoint ep = VAEntrypointEncSlice;
            bool canEncode = VaHasEntrypoint(dpy, VAProfileH264Main, ep) ||
                             VaHasEntrypoint(dpy, VAProfileH264High, ep);
            if (!canEncode) {
                ep = VAEntrypointEncSliceLP;
                canEncode = VaHasEntrypoint(dpy, VAProfileH264Main, ep) ||
                            VaHasEntrypoint(dpy, VAProfileH264High, ep);
            }
            if (!canEncode) {
                LOGI("[VA] %s: no H.264 encode entrypoint, skipping.", path.c_str());
                vaTerminate(dpy);
                close(fd);
                continue;
            }

            dpy_ = dpy;
            drmFd_ = fd;
            devicePath_ = path;
            encEntrypoint_ = ep;
            const char* vendor = vaQueryVendorString(dpy);
            driverName_ = vendor ? vendor : "?";
            LOGI("[VA] %s — VA-API %d.%d, driver: %s, entrypoint: %s", path.c_str(), major, minor,
                driverName_.c_str(), ep == VAEntrypointEncSliceLP ? "EncSliceLP (low power)" : "EncSlice");
            return;
        }

        lastError_ =
            "no GPU with VA-API H.264 encoding found — run `vainfo` to see what your card "
            "reports: it needs VAProfileH264Main or High with VAEntrypointEncSlice or "
            "EncSliceLP. If vainfo shows nothing, install the driver for your card "
            "(intel-media-va-driver / mesa-va-drivers / nvidia-vaapi-driver) and make sure you "
            "can open /dev/dri/renderD* (group 'render', or an ACL entry from your desktop "
            "session)";
        LOGE("[VA] %s", lastError_.c_str());
    });
    return dpy_ != nullptr;
}
