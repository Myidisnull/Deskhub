#include "Tests.h"
#include "support/TestSupport.h"

#include <cstdio>
#include <string_view>

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

int main(int argc, char** argv) {
    KeepTestLogsOutOfTheDeveloperHome();

    const bool onlyUnderLoad = argc > 1 && std::string_view(argv[1]) == "--under-load";

    std::printf("=== integration self-test (host + viewer over loopback, fake codecs) ===\n");

    if (!onlyUnderLoad) {
        std::printf("--- wire: golden byte vectors (same on every OS) ---\n");
        RunWireVectorTests();

        std::printf("--- end to end: connect, stream, input, disconnect ---\n");
        RunSessionFlowTests();
    }

    std::printf("--- under load: a file transfer beside a live stream ---\n");
    RunTransferUnderLoadTests();

    std::printf("--- under load: a terminal beside video and bulk transfer ---\n");
    RunLagUnderCrossLoadTests();

    if (g_failures == 0) {
        std::printf("=== PASS: all checks passed ===\n");
        return 0;
    }
    std::printf("=== FAIL: %d checks failed ===\n", g_failures);
    return 1;
}
