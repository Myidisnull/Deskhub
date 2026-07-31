#include "Tests.h"
#include "support/TestSupport.h"

#include <cstdio>

int main() {
    std::printf("=== core self-test (offline: no network, no GPU) ===\n");

    std::printf("--- wire ---\n");
    RunWireTests();

    std::printf("--- transport: reassembler ---\n");
    RunReassemblerTests();

    std::printf("--- transport: FEC ---\n");
    RunFecTests();

    std::printf("--- transport: retransmit/NACK ---\n");
    RunRetransmitCacheTests();

    std::printf("--- session ---\n");
    RunSessionTests();

    std::printf("--- input ---\n");
    RunInputTests();

    std::printf("--- control: bitrate + link stats ---\n");
    RunControlTests();

    std::printf("--- control: stream size negotiation ---\n");
    RunStreamSizeTests();

    std::printf("--- control: clock offset / one-way latency ---\n");
    RunClockOffsetTests();

    std::printf("--- control: quality ladder (fps + resolution vs bandwidth) ---\n");
    RunQualityLadderTests();

    std::printf("--- diag: window counters + log line formatting ---\n");
    RunDiagTests();

    std::printf("--- media: encoder/decoder signature contract ---\n");
    RunMediaContractTests();

    std::printf("--- beacon (pre-session LIST_SOURCES + PING) ---\n");
    RunBeaconTests();

    if (g_failures == 0) {
        std::printf("=== PASS: all checks passed ===\n");
        return 0;
    }
    std::printf("=== FAIL: %d checks failed ===\n", g_failures);
    return 1;
}
