#pragma once
#include <cstdarg>
#include <cstdio>

namespace deskhub::diag {

inline void Append(char*& p, char* end, const char* fmt, ...) {
    if (p >= end) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(p, size_t(end - p), fmt, ap);
    va_end(ap);
    p = (n < 0 || n >= int(end - p)) ? end : p + n;
}

}
