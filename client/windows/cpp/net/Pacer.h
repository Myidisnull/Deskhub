#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>

class Pacer {
public:
    Pacer() = default;
    ~Pacer();
    Pacer(const Pacer&) = delete;
    Pacer& operator=(const Pacer&) = delete;

    void SetRateBps(uint64_t rateBps) {
        rateBps_ = rateBps;
    }

    void Gate(size_t bytes);

private:
    void SleepUs(uint64_t us);

    uint64_t rateBps_ = 0;
    uint64_t nextUs_ = 0;
    HANDLE timer_ = nullptr;
};
