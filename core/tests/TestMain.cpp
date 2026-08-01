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

    std::printf("--- transport: send pacer ---\n");
    RunPacerTests();

    std::printf("--- session ---\n");
    RunSessionTests();

    std::printf("--- session: client pump (ingest + keyframes + reporting) ---\n");
    RunClientPumpTests();

    std::printf("--- session: host feedback policy (bitrate, FEC, quality, NACK) ---\n");
    RunHostFeedbackTests();

    std::printf("--- session: host router (demux, re-offer, keepalive timing) ---\n");
    RunHostRouterTests();

    std::printf("--- session: viewer connect flow ---\n");
    RunConnectFlowTests();

    std::printf("--- input ---\n");
    RunInputTests();

    std::printf("--- input: held keys/buttons + host-wins gate ---\n");
    RunPressedInputTests();

    std::printf("--- input: client-side queue (taps, chords, delayed release) ---\n");
    RunClientInputQueueTests();

    std::printf("--- input: shared scancode table lookups ---\n");
    RunScancodeTableTests();

    std::printf("--- input: shared on-screen hotkey bar ---\n");
    RunHotkeysTests();

    std::printf("--- input: pointer mapping + shared injector dispatch ---\n");
    RunPointerMapTests();

    std::printf("--- control: bitrate + link stats ---\n");
    RunControlTests();

    std::printf("--- control: stream size negotiation ---\n");
    RunStreamSizeTests();

    std::printf("--- control: per-source frame rate gate ---\n");
    RunFrameGateTests();

    std::printf("--- control: clock offset / one-way latency ---\n");
    RunClockOffsetTests();

    std::printf("--- control: quality ladder (fps + resolution vs bandwidth) ---\n");
    RunQualityLadderTests();

    std::printf("--- diag: window counters + log line formatting ---\n");
    RunDiagTests();

    std::printf("--- media: encoder/decoder signature contract ---\n");
    RunMediaContractTests();

    std::printf("--- media: H.264 bit writer (exp-golomb + emulation prevention) ---\n");
    RunBitWriterTests();

    std::printf("--- media: H.264 SPS rewrite (VUI with zero reorder delay) ---\n");
    RunH264SpsTests();

    std::printf("--- media: Annex-B NAL parsing ---\n");
    RunAnnexBTests();

    std::printf("--- media: encoder rate plan (VBV sizing) ---\n");
    RunRatePlanTests();

    std::printf("--- media: share quality presets ---\n");
    RunQualityPresetTests();

    std::printf("--- media: view fit (letterbox, zoom/pan, pointer mapping) ---\n");
    RunViewFitTests();

    std::printf("--- beacon (pre-session LIST_SOURCES + PING) ---\n");
    RunBeaconTests();

    if (g_failures == 0) {
        std::printf("=== PASS: all checks passed ===\n");
        return 0;
    }
    std::printf("=== FAIL: %d checks failed ===\n", g_failures);
    return 1;
}
