#pragma once
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

inline uint64_t NowUs() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) f.QuadPart = 1;
        return f;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart / freq.QuadPart) * 1'000'000ull +
           (uint64_t)(c.QuadPart % freq.QuadPart) * 1'000'000ull / (uint64_t)freq.QuadPart;
}

#else
#include <ctime>

inline uint64_t NowUs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1'000'000ull + uint64_t(ts.tv_nsec) / 1000ull;
}

#endif
