#include "support/TestSupport.h"

#include "deskhubp/system/Clock.h"

#include <cstdio>

#ifdef _WIN32
#include <processthreadsapi.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr uint16_t kFirstTestPort = 40001;
constexpr uint16_t kPortsPerProcess = 64;
constexpr uint16_t kProcessSlots = 64;
constexpr uint16_t kFirstEphemeralPort = 49152;

static_assert(kProcessSlots > 0 && (kProcessSlots & (kProcessSlots - 1)) == 0,
    "the slot count must be a power of two, since the pid hash is folded with a mask");
static_assert(kFirstTestPort + kProcessSlots * kPortsPerProcess <= kFirstEphemeralPort,
    "the whole slot range must stay below the ephemeral range Windows hands out, or a "
    "concurrent process can be given a port a test is about to bind");

uint32_t ThisProcessId() {
#ifdef _WIN32
    return uint32_t(GetCurrentProcessId());
#else
    return uint32_t(getpid());
#endif
}

}

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
    static uint16_t next =
        kFirstTestPort +
        uint16_t(ThisProcessId() * 2654435761u >> 24 & (kProcessSlots - 1u)) * kPortsPerProcess;
    return next++;
}
