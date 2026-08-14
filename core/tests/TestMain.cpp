#include "Tests.h"
#include "support/TestSupport.h"

#include <cstdio>

int main() {
    std::printf("=== core self-test (offline: no network, no GPU) ===\n");

    std::printf("--- wire: big-endian field accessors ---\n");
    RunByteOrderTests();

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

    std::printf("--- session: clipboard sync (chunking, dedupe, echo suppression) ---\n");
    RunClipboardSyncTests();

    std::printf("--- session: host feedback policy (bitrate, FEC, quality, NACK) ---\n");
    RunHostFeedbackTests();

    std::printf("--- session: host router (demux, re-offer, keepalive timing) ---\n");
    RunHostRouterTests();

    std::printf("--- session: many viewers per source (fan-out, input priority) ---\n");
    RunHostViewersTests();

    std::printf("--- session: viewer connect flow ---\n");
    RunConnectFlowTests();

    std::printf("--- session: elevated share args + source clamp ---\n");
    RunShareFlowTests();

    std::printf("--- session: open viewer count ---\n");
    RunOpenViewersTests();

    std::printf("--- session: per-source pipeline state defaults ---\n");
    RunSourcePipelineStateTests();

    std::printf("--- session: remote terminal (host sessions, client, stream framing) ---\n");
    RunTerminalSessionTests();

    std::printf("--- input ---\n");
    RunInputTests();

    std::printf("--- input: held keys/buttons + host-wins gate ---\n");
    RunPressedInputTests();

    std::printf("--- input: client-side queue (taps, chords, delayed release) ---\n");
    RunClientInputQueueTests();

    std::printf("--- input: shared scancode table lookups ---\n");
    RunScancodeTableTests();

    std::printf("--- input: the vk -> set-1 scancode table as a whole ---\n");
    RunSet1ScancodeTests();

    std::printf("--- input: shared on-screen hotkey bar ---\n");
    RunHotkeysTests();

    std::printf("--- input: pointer mapping + shared injector dispatch ---\n");
    RunPointerMapTests();

    std::printf("--- input: viewer pointer lock ---\n");
    RunPointerLockStateTests();

    std::printf("--- input: touch trackpad cursor ---\n");
    RunTrackpadCursorTests();

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

    std::printf("--- media: H.264 macroblock geometry and levels ---\n");
    RunH264EncodeTests();

    std::printf("--- media: share quality presets ---\n");
    RunQualityPresetTests();

    std::printf("--- media: view fit (letterbox, zoom/pan, pointer mapping) ---\n");
    RunViewFitTests();

    std::printf("--- media: source names and size labels ---\n");
    RunSourceLabelTests();

    std::printf("--- media: viewer window title (status + lock hint) ---\n");
    RunViewerTitleTests();

    std::printf("--- beacon (pre-session LIST_SOURCES + PING) ---\n");
    RunBeaconTests();

    std::printf("--- net: which addresses a LAN scan should try ---\n");
    RunLanScanTests();

    std::printf("--- net: dotted-quad IPv4 parsing ---\n");
    RunIpv4Tests();

    std::printf("--- net: which address the host binds ---\n");
    RunBindAddressTests();

    std::printf("--- net: which host keys we have decided to trust ---\n");
    RunTrustStoreTests();

    std::printf("--- terminal: the VT escape-sequence parser ---\n");
    RunVtParserTests();

    std::printf("--- terminal: the character grid every client draws ---\n");
    RunScreenTests();

    std::printf("--- terminal: keys and modifiers to bytes ---\n");
    RunKeyEncoderTests();

    std::printf("--- terminal: the colours every client paints with ---\n");
    RunPaletteTests();

    std::printf("--- ui: shared strings every client shows ---\n");
    RunStringsTests();

    std::printf("--- ui: host table rows (displays, viewers, cells) ---\n");
    RunHostRowsTests();

    std::printf("--- ui: recent devices list (parse, touch, cap) ---\n");
    RunRecentDevicesTests();

    std::printf("--- ui: persisted share settings ---\n");
    RunUiSettingsTests();

    std::printf("--- ui: launch-at-login artifacts ---\n");
    RunAutostartConfigTests();

    std::printf("--- ui: passcodes stored on disk ---\n");
    RunSecretTextTests();

    std::printf("--- fuzz: deterministic structured fuzzing over every parser ---\n");
    RunFuzzTests();

    if (g_failures == 0) {
        std::printf("=== PASS: all checks passed ===\n");
        return 0;
    }
    std::printf("=== FAIL: %d checks failed ===\n", g_failures);
    return 1;
}
