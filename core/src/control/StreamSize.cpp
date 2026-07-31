#include "deskhub/control/StreamSize.h"

namespace deskhub {

namespace {

uint32_t Even(double v) {
    if (v < 2.0) return 0;
    return uint32_t(v) & ~1u;
}

}

StreamSize FitStreamSize(uint32_t srcW, uint32_t srcH, uint32_t maxDim, uint32_t clientW,
    uint32_t clientH) {
    if (!srcW || !srcH) return {};

    const double sw = double(srcW), sh = double(srcH);
    double f = 1.0;

    if (maxDim) {
        const double longEdge = sw > sh ? sw : sh;
        const double fCap = double(maxDim) / longEdge;
        if (fCap < f) f = fCap;
    }

    if (clientW && clientH) {
        const double cw = clientW > clientH ? double(clientW) : double(clientH);
        const double ch = clientW > clientH ? double(clientH) : double(clientW);
        const double fw = cw / sw, fh = ch / sh;
        const double fCli = fw < fh ? fw : fh;
        if (fCli < f) f = fCli;
    }

    if (f >= 1.0) return {srcW, srcH};

    const uint32_t w = Even(sw * f), h = Even(sh * f);
    if (!w || !h) return {srcW, srcH};
    return {w, h};
}

}
