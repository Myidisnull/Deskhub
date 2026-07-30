// =============================================================================
// StreamSize.cpp — cài đặt FitStreamSize. Lý do thiết kế ở control/StreamSize.h.
//
// CÁCH LÀM: GOM MỌI TRẦN THÀNH MỘT HỆ SỐ CO DUY NHẤT
//   Mỗi trần cho ra một hệ số "nguồn phải co bao nhiêu để lọt". Lấy hệ số NHỎ NHẤT
//   rồi nhân một lần vào cả hai cạnh. Co từng bước theo từng trần thì mỗi bước lại
//   làm tròn chẵn một lần, và hai lần làm tròn liên tiếp đẩy tỉ lệ lệch đủ để sinh
//   viền đen — thứ ta đang cố tránh.
// =============================================================================
#include "deskhub/control/StreamSize.h"

namespace deskhub {

namespace {

// Làm tròn XUỐNG số chẵn. Dưới 2 thì không còn khung nào hợp lệ → 0, và caller ở
// dưới hiểu 0 là "co hụt, trả lại cỡ gốc".
uint32_t Even(double v) {
    if (v < 2.0) return 0;
    return uint32_t(v) & ~1u;
}

} // namespace

StreamSize FitStreamSize(uint32_t srcW, uint32_t srcH, uint32_t maxDim, uint32_t clientW,
    uint32_t clientH) {
    if (!srcW || !srcH) return {};

    const double sw = double(srcW), sh = double(srcH);
    double f = 1.0;

    // Trần 1: cạnh dài của khung không vượt maxDim.
    if (maxDim) {
        const double longEdge = sw > sh ? sw : sh;
        const double fCap = double(maxDim) / longEdge;
        if (fCap < f) f = fCap;
    }

    // Trần 2: khung phải lọt vào màn hình client — tính ở hướng NẰM NGANG, tức cạnh
    // dài của client làm bề rộng (xem cảnh báo ở .h: điện thoại xoay được và ta chỉ
    // chốt một lần).
    if (clientW && clientH) {
        const double cw = clientW > clientH ? double(clientW) : double(clientH);
        const double ch = clientW > clientH ? double(clientH) : double(clientW);
        const double fw = cw / sw, fh = ch / sh;
        const double fCli = fw < fh ? fw : fh;
        if (fCli < f) f = fCli;
    }

    // Không bao giờ phóng to.
    if (f >= 1.0) return {srcW, srcH};

    const uint32_t w = Even(sw * f), h = Even(sh * f);
    // Trần bé tới mức làm tròn ra 0 (client báo số vô lý, hoặc maxDim đặt tay quá
    // nhỏ): thà phát native còn hơn trả về một cỡ không nén được rồi chết phiên.
    if (!w || !h) return {srcW, srcH};
    return {w, h};
}

} // namespace deskhub
