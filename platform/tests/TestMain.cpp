#include "Tests.h"
#include "support/TestSupport.h"

#include <cstdio>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdlib>

namespace {

constexpr const char* kTestHome = "/tmp/deskhub-platform-tests";

void KeepTestLogsOutOfTheDeveloperHome() {
    if (mkdir(kTestHome, 0700) == 0 || errno == EEXIST) setenv("HOME", kTestHome, 1);
}

}
#else
namespace {
void KeepTestLogsOutOfTheDeveloperHome() {}
}
#endif

int main() {
    KeepTestLogsOutOfTheDeveloperHome();

    std::printf("=== platform self-test (local only: loopback sockets, no remote peer) ===\n");

    std::printf("--- system: monotonic clock + sleep ---\n");
    RunClockTests();

    std::printf("--- system: OS randomness ---\n");
    RunRandomTests();

    std::printf("--- diag: log file naming + timestamps ---\n");
    RunLogFileTests();

    std::printf("--- net: address parsing, printing and packing ---\n");
    RunNetAddrTests();

    std::printf("--- net: per-viewer client id ---\n");
    RunClientIdTests();

    std::printf("--- net: UDP socket over loopback + local adapters ---\n");
    RunUdpSocketTests();

    std::printf("--- ffi: string handover to the managed clients ---\n");
    RunFfiTextTests();

    std::printf("--- input: local-input gate shared by every injector ---\n");
    RunLocalInputGateTests();

    std::printf("--- session: host callbacks wired to a source pipeline ---\n");
    RunHostCallbackTests();

    if (g_failures == 0) {
        std::printf("=== PASS: all checks passed ===\n");
        return 0;
    }
    std::printf("=== FAIL: %d checks failed ===\n", g_failures);
    return 1;
}
