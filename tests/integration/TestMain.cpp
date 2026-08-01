#include "Tests.h"
#include "support/TestSupport.h"

#include <cstdio>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdlib>

namespace {

constexpr const char* kTestHome = "/tmp/deskhub-integration-tests";

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

    std::printf("=== integration self-test (host + viewer over loopback, fake codecs) ===\n");

    std::printf("--- wire: golden byte vectors (same on every OS) ---\n");
    RunWireVectorTests();

    std::printf("--- end to end: connect, stream, input, disconnect ---\n");
    RunSessionFlowTests();

    if (g_failures == 0) {
        std::printf("=== PASS: all checks passed ===\n");
        return 0;
    }
    std::printf("=== FAIL: %d checks failed ===\n", g_failures);
    return 1;
}
