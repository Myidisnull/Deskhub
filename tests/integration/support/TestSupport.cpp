#include "support/TestSupport.h"

#include "deskhubp/system/Clock.h"

#include <cstdio>

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

bool WaitFor(const std::function<bool()>& done, uint32_t timeoutMs) {
    const uint64_t deadlineUs = NowUs() + uint64_t(timeoutMs) * 1000ull;
    for (;;) {
        if (done()) return true;
        if (NowUs() >= deadlineUs) return false;
        SleepUs(2'000);
    }
}

std::string Hex(std::span<const uint8_t> bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0F]);
    }
    return out;
}

uint16_t NextTestPort() {
    static uint16_t next = 47901;
    return next++;
}
