# 06 — Transport Internals

How an encoded video frame travels from the host to the client over UDP, and how the
two ends cope with loss, reordering, and congestion. This documents the transport and
congestion-control layers as implemented today:

- **Host side**: `Packetizer`, `RetransmitCache`, `BitrateController` (all in `core/`),
  plus the platform `Pacer` (`client/windows/cpp/net/Pacer.h`).
- **Client side**: `Reassembler`, `LinkStats`, `ClockSync`, `LatencyTrace` (all in `core/`).

The byte-level wire format lives in `core/include/deskhub/wire/Wire.h` and is specified
in 04-protocol.md. Where the whole system sits is 01-architecture.md; the per-platform
socket wrappers are 11-platform-transport.md; the diagnostics built on these counters
are 09-diagnostics.md. Behavior described here is pinned down by
`core/tests/transport/` (FecTests, ReassemblerTests, RetransmitCacheTests) and
`core/tests/control/ControlTests.cpp` — the tests are the executable version of this doc.

## 1. Design goals and constraints

- **UDP, one datagram at a time.** Every message fits one datagram of at most
  `kMaxDatagram = 1200` bytes (Internet-MTU safe). No IP fragmentation, no reliance on
  ordering or delivery.
- **Latency first.** A frame that cannot be completed in time is *dropped*, never
  waited for indefinitely. The decoder is fed frames strictly in order or not at all.
- **Infinite GOP.** The encoder emits no periodic IDR frames (an IDR is many times the
  size of a P-frame). Every frame references the previous one, so a single lost frame
  poisons everything after it until a fresh IDR arrives — the recovery machinery below
  (FEC, NACK, IDR-on-demand) exists to make that event rare and short.
- **Core is platform-free and clock-free.** Nothing in `core/` owns a socket, a thread,
  or a clock. Datagrams leave through `send` callbacks; time is injected as `nowUs`
  parameters. This is what lets the tests wire a `Packetizer` directly into a
  `Reassembler`, drop packets in between, and fast-forward time without sleeping.
- **No allocation on the hot path.** `Packetizer` keeps member buffers; `Reassembler`,
  `RetransmitCache`, and `LatencyTrace` reuse vectors/rings; steady state allocates
  only when a frame is larger than any frame seen before.

Key wire constants (`core/include/deskhub/wire/Wire.h`):

| Constant | Value | Meaning |
|---|---|---|
| `kMaxDatagram` | 1200 | max datagram size on the wire |
| `kCommonHeaderSize` | 8 | ver, type, flags, chan, sessionId |
| `kVideoHeaderSize` / `kFecHeaderSize` | 16 | video/FEC subheader after the common header |
| `kFecLenPrefix` | 2 | XOR-of-lengths prefix inside FEC parity |
| `kMaxVideoPayload` | 1174 | payload per video fragment (1200 − 8 − 16 − 2) |
| `kFecGroupSize` | 8 | target FEC group size → 1/8 bandwidth overhead |
| `kMaxFecGroups` | 256 | `groupIndex` is u8; beyond this FEC is skipped |
| `kMaxNackIndices` | 593 | max fragment indices in one NACK datagram |

## 2. Packetizer — frame → datagrams (host)

`core/include/deskhub/transport/Packetizer.h`, `core/src/transport/Packetizer.cpp`.
Runs on the per-source encode thread (the WGC `FrameArrived` thread on Windows); it is
single-threaded and unlocked, so each encode thread owns its own instance.

`Packetizer::SendFrame(nal, frameId, timestampUs, idr, send)`:

- Splits the Annex-B frame into `count = ceil(size / kMaxVideoPayload)` fragments.
  **Every fragment except the last carries exactly `kMaxVideoPayload` bytes**, so the
  receiver derives each fragment's byte offset from `pktIndex` alone — there is no
  offset field on the wire.
- Each fragment becomes one `VIDEO_PACKET` (`BuildVideoPacket`) carrying
  `VideoHeader{frameId, timestampUs, pktIndex, pktCount}` plus two flags:
  `kVideoFlagIdr` on every fragment of an IDR frame, and `kVideoFlagFrameEnd` on the
  last fragment. Fragments are handed to `send` in increasing `pktIndex` order.
- Returns the number of data packets sent; returns 0 (sends nothing) for an empty
  frame, a missing callback, or a frame needing more than 65535 fragments
  (`pktIndex`/`pktCount` are u16).

**FEC generation** (enabled per frame via `SetFecEnabled`, driven by the host's
`BitrateController` — off by default because parity costs 1/`kFecGroupSize` = 12.5%
bandwidth):

- The frame's fragments are divided into `numGroups = ceil(count / kFecGroupSize)`
  **interleaved** groups: fragment `i` belongs to group `i % numGroups`. Members of a
  group are `numGroups` positions apart, so a burst of up to `numGroups` *consecutive*
  losses touches each group at most once and remains fully recoverable
  (`TestFecInterleavedBurst`).
- One parity packet per group (`FEC_PACKET`, `BuildFecPacket`), each
  `kParityStride = kFecLenPrefix + kMaxVideoPayload = 1176` bytes: a 2-byte XOR of the
  member lengths (the final fragment of a frame is shorter, and the receiver cannot
  guess a missing packet's length otherwise), then the XOR of the member payloads
  zero-padded to full stride.
- Parity is accumulated in the same single pass that emits the data packets (the frame
  is never read twice), and all parity packets are sent **after** the last data packet
  — interleaved groups only close at the end of the frame, and by then the receiver
  already knows exactly what is missing.
- If `numGroups > kMaxFecGroups` (frames above 2048 fragments, ≈ 2.3 MB), the u8
  `groupIndex` cannot number the groups: the frame is sent plain, with no parity
  (`TestFecTooManyGroupsSendsPlain`).

## 3. Reassembler — datagrams → frames (client)

`core/include/deskhub/transport/Reassembler.h`, `core/src/transport/Reassembler.cpp`.
Runs on the client's Recv thread; the only place in the client that understands
"packets". Holds at most `kMaxPendingFrames = 4` partially assembled frames in a
`std::map` keyed by `frameId` (ordered, so `begin()` is always the next frame due).

**Ingest** (`Push` / `PushFec` → `Slot`):

- Every data packet increments `stats_.packetsReceived` *before* any validation —
  including duplicates and stragglers — because it is the denominator of the reported
  loss rate. Parity packets are counted separately (`fecReceived`) and deliberately
  excluded from that denominator: mixing them in would deflate the loss figure exactly
  when FEC is on, i.e. exactly when the link is in trouble.
- A **barrier** (`barrierId_`) marks the highest frameId already emitted or dropped.
  Packets at or below it are too late to matter and are rejected at `Slot()`.
- Duplicates (slot already filled), out-of-range `pktIndex`, and packets whose
  `pktCount` disagrees with the pending frame are ignored (`TestPktCountMismatch`).
- A new frameId when all 4 slots are busy evicts the oldest pending frame
  (`DropReason::Evicted`).
- The gap between consecutive data packets is tracked; `TakeMaxGapMs()` is a
  read-and-clear "worst silence" sensor (hundreds of ms ≈ Wi-Fi power-save or
  congestion buffering upstream).

**FEC recovery** (`TryRecover`): if a group's parity is present and the group is
missing *exactly one* fragment, that fragment is rebuilt by XOR-ing the parity with
every received member (lengths first, then payload). Recovery is attempted from both
`Push` and `PushFec`, since either the last data packet or the parity may arrive last.
The reconstructed length is validated (`0 < len ≤ kMaxVideoPayload` and within the
parity buffer); corrupt parity is refused rather than injecting garbage into the NAL
(`TestFecCorruptParityRejected`). Two losses in the same group are unrecoverable — one
equation cannot solve two unknowns (`TestFecInterleavedSameGroup`).

**Emission and drop policy** (`PopReady`, called with the current time): only the
*oldest* pending frame is ever considered — the H.264 decoder needs strict order, so a
newer complete frame never jumps the queue. Per iteration, one of three things happens:

1. Head complete → concatenate fragments in `pktIndex` order into one Annex-B buffer,
   advance the barrier, return the frame. (Unless the reassembler is *waiting for
   IDR* — after a join or a loss — in which case complete non-IDR frames are
   swallowed: `DropReason::PreIdr`, counted as `framesSkipped`, not loss.)
2. Head incomplete and hopeless → drop it and re-examine the queue:
   - `Timeout`: more than **2 frame intervals** have passed since its first fragment
     arrived (`frameIntervalUs_`, defaulting to 16 667 µs ≈ 60 fps).
   - `Overtaken`: **≥ 2 newer frames** are already complete behind it.
3. Otherwise → return `nullopt`; the missing fragment may still be in flight.

A drop for real loss (`Timeout`/`Overtaken`/`Evicted`) sets the one-shot loss event
(`TakeLossEvent`) and `waitingForIdr_`, records the missing-run histogram
(`Stats::lossRuns`, doubling buckets 1, 2, 3, 4–7, 8–15, 16–31, ≥ 32 — the number that
actually decides FEC design, since runs ≥ 2 inside one group defeat XOR parity), and
buries the frameId in a 16-entry **graveyard** (~0.25 s @ 60 fps). Packets of a buried
frame that straggle in later are counted as *late*, not lost
(`Stats::latePackets/lateMsSum/lateMsMax`) — the measurement that distinguishes "the
network dropped it" from "the reassembly deadline expired first", which call for
opposite fixes. Every drop also invokes the optional `onFrameDrop` callback with a
`FrameDropInfo` autopsy (missing count and positions, wait time, bytes received).

## 4. NACK and the RetransmitCache

FEC repairs single losses with zero added latency but costs 12.5% always-on overhead
and cannot fix ≥ 2 losses in a group. NACK is the complement: it costs bandwidth only
when loss actually happens and can repair any pattern — provided the RTT is small
relative to the reassembly deadline (LAN RTT of a few ms vs ~33 ms @ 60 fps).

**Client side** — `Reassembler::PlanNack(nowUs, rttUs, frameId, out)` plans a request
for the *oldest* incomplete pending frame:

- Waits `kNackHoldUs = 2 ms` after the frame's first fragment before concluding
  anything is missing (gives reordered packets a beat; no wasted requests).
- Skips frames already past the reassembly deadline (≥ 2 frame intervals) — a
  retransmit could not arrive in time anyway.
- Re-requests the same frame no more often than `max(rttUs, kNackMinIntervalUs = 10 ms)`;
  a retransmitted packet needs ~1 RTT to arrive, so asking sooner only doubles traffic.
  The `rttUs` argument comes from the client's measured RTT (0 if not yet known).
- Lists the missing `pktIndex` values (clamped to the caller's span). The caller wraps
  them with `BuildNack` and sends via `ClientSession::SendNack`. NACK is best-effort
  *on top of* the drop policy, never a replacement: if the retransmit beats the
  deadline, the frame is saved without an IDR (`TestNackEndToEnd`); if not, the normal
  `Timeout` drop fires.

**Host side** — `core/include/deskhub/transport/RetransmitCache.h`:

- The Packetizer's send callback copies **every video datagram verbatim** into the
  cache (`Store`); the cache parses the headers itself (`ParseCommonHeader` +
  `ParseVideoPacket`), so it is always consistent with what actually left the machine.
  FEC and control packets are ignored — parity is reproducible, control has its own
  retry machinery.
- Storage is a ring of `kCacheFrames = 8` frame slots (~0.13 s @ 60 fps, longer than
  the client's 2-frame-interval deadline). The ring advances per *frame*, not per
  packet: a many-fragment frame never evicts its own earlier fragments.
- On NACK (`onNack` in `client/windows/cpp/AgentLoop.cpp`), the host looks up each
  requested `(frameId, pktIndex)` with `Find` and resends the bytes as-is. `Store` runs
  on the encode thread and `Find` on the Recv thread, so `AgentLoop` guards the pair
  with a mutex (`retxMutex`) — the class itself is unlocked by design. `Reset()` clears
  the cache on disconnect so a new session never receives the previous session's bytes.

**IDR-on-demand is the backstop.** When `TakeLossEvent()` fires (or while
`WaitingForIdr()` is true), the client calls `ClientSession::RequestKeyframe`; `Tick`
repeats `REQUEST_KEYFRAME` every 250 ms until `CancelKeyframeRequest` when an IDR
arrives. On the host, `onKeyframeRequest` merely sets the atomic `forceIdr` flag; the
encode thread consumes it on the next frame. NACK exists precisely to make this path
rare — an IDR is the most expensive possible answer to loss, sent into an
already-struggling link.

## 5. Pacing — smoothing send bursts (host, Windows)

`client/windows/cpp/net/Pacer.h/.cpp`. This lives in the platform tree, not `core/`,
because only the video sender needs it and it requires both a clock and the ability to
sleep — two things `core/` deliberately does not have.

**The problem it addresses**: `Packetizer::SendFrame` emits all fragments in a tight
loop, so a 450 KB IDR leaves a 1 Gbps NIC as one continuous burst. At the first slower
hop (e.g. ~300 Mbps Wi-Fi) a queue must absorb the whole burst; when it overflows it
tail-drops a contiguous *run* of packets. Field measurement (Pixel 4, see
docs/08-android-client.md) matched exactly: nearly all loss runs were ≥ 32 packets,
the longest 384 packets (~450 KB) — a shape no XOR parity can repair (recovering 384
losses would need 384 parity packets, i.e. 100% overhead). The fix is at the source:
don't send bursts.

**Mechanism** — the entire state is one timestamp, `nextUs_` (earliest permitted send
time). Each `Gate(bytes)` call, made immediately before `sendto`:

1. If `nextUs_` is in the past, snap it to now — idle time must **not** accumulate
   credit, or the next frame would burst again.
2. If the required wait is at least `kMinSleepUs = 500`, sleep for it. Shorter debts
   just accumulate, so packets leave in ~500 µs micro-bursts instead of one
   syscall-heavy sleep per packet (~300 µs/packet at 60 Mbps) that no bottleneck queue
   could distinguish anyway.
3. Add `bytes × 8 × 1 000 000 / rateBps_` µs to `nextUs_`.

Sleeping uses a lazily created `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` waitable timer
(Windows 10 1803+) — plain `Sleep()` quantizes to the scheduler tick (up to ~15.6 ms)
and would destroy sub-millisecond pacing; the fallback on old machines is coarse
`Sleep`. `SetRateBps(0)` disables pacing entirely.

**Threading rule (learned the hard way)**: `Gate` blocks, so it must run on a dedicated
send thread — never inside the encoder's `onPacket`, which executes synchronously
inside WGC's `FrameArrived` callback; sleeping there stalls capture itself (the first
attempt did exactly that: throughput collapsed to ~0 kbps, e2e hit 13.8 s). Note the
current state of the wiring: `client/windows/cpp/AgentLoop.cpp` today sends directly
from `onPacket` on the capture/encode thread (where sleeping is forbidden) and
instruments each frame's send-burst duration (`dgBurstMsMax`, `dgIdrBurstMs`) for
diagnosis; `Pacer` is the host-side smoothing component intended for a dedicated send
thread and is not gated into that path yet.

## 6. LinkStats — measuring the last second (client)

`core/include/deskhub/control/LinkStats.h`, `core/src/control/LinkStats.cpp`. The
`Reassembler` counts cumulatively from session start; what the overlay and the host
need is *the last window*. `LinkStats` keeps the previous snapshot and diffs.

- `Due(nowUs)` — true once `windowUs` (default 1 s) has elapsed; the client polls it
  each loop iteration. `Close(stats, videoBytes, renderedFrames, nowUs)` computes the
  window **and** commits the new snapshot — it has side effects; call it once per
  window.
- Rates divide by the *measured* window length, not the nominal 1 s (the recv loop
  blocks in `recvfrom` up to 100 ms, so windows run long; dividing by a constant would
  inflate fps/kbps — `TestLinkStatsUsesRealElapsed`).
- Per-window outputs (`LinkWindow`): `fps` (rendered frames), `kbps` (received video
  bytes), `lossPct = 100 × lost / (lost + received)` with parity excluded from the
  denominator, `packetsRecovered`, `framesDropped`, the loss-run bucket deltas
  (`lossRunMax` is a cumulative record, copied through), late-packet stats (per-window
  average, cumulative max), and e2e latency aggregated via `AddE2e` once per displayed
  frame (avg/max/samples; `e2eSamples == 0` means "draw a dash, not 0 ms").
- `MakeFeedback(window, rttUs)` compresses the window into the 9-byte `Feedback`
  message: `lostFrames` (u16), `lossPct` (u8, rounded to nearest), `rttMs` (u16),
  `recvBitrateKbps` (u32). The client sends it **every** window, even when perfectly
  clean — the host needs the "link is clean" signal to dare ramping bitrate back up;
  silence reads as disconnection. RTT itself is measured only on the client (PING every
  `kPingIntervalUs = 1 s` from `ClientSession::Tick`; PONG echoes the send time), so
  `Feedback.rttMs` is also the only way that number reaches the host.

## 7. BitrateController — the adaptation policy (host)

`core/include/deskhub/control/BitrateController.h`,
`core/src/control/BitrateController.cpp`. One controller per source (each source has
its own encoder and its own client), called once per received `Feedback` (~1/s) from
`onFeedback` in `client/windows/cpp/AgentLoop.cpp`. Pure policy: no encoder, no
socket, no clock — `nowUs` is injected.

Two independent decisions on two threshold scales (the 1–2% band is a deliberate
buffer: enough loss to justify cheap FEC, not enough to degrade picture quality):

**FEC** — on immediately at `lossPct ≥ 1`; off only after **5 consecutive clean
updates** (`cleanSeconds_` counts *calls*, relying on the once-per-second feedback
convention; a slower sender only delays the switch-off, which merely costs bandwidth).
Loss tends to come in clusters — switching off after one quiet second would mean
toggling constantly (`TestFecHysteresis`). `fecToggled` marks transitions for one-line
logging. The FEC state commits unconditionally; `AgentLoop` forwards it through the
`wantFec` atomic to `Packetizer::SetFecEnabled` on the encode thread.

**Bitrate** — multiplicative decrease, slow additive recovery:

| Condition | Action |
|---|---|
| `lossPct ≥ 5` | `next = cur − cur/4` (×0.75), stamp `lastDecreaseUs` |
| `lossPct ≥ 2` | `next = cur − cur/10` (×0.90), stamp `lastDecreaseUs` |
| `lossPct ≤ 1` **and** > 2 s since the last decrease | `next = cur + max/20` (+5% of the *ceiling* per update) |

The result is clamped to `[minBps, startBps]` — the user-chosen start bitrate is also
the ceiling, and the floor is never crossed under sustained loss
(`TestBitrateBackoff`). Changes smaller than 2% of the current rate (`delta < cur/50`)
are suppressed: every change is a rate-control renegotiation with the encoder, not
worth a few tens of kbps.

**Propose/commit split**: `Update` only *proposes*. The caller first tries
`encoder->SetBitrate(...)`; only on success does it call `CommitBitrate`, so a rejected
change leaves the controller recomputing from the old rate next second
(`TestBitrateUncommitted`).

## 8. ClockSync and LatencyTrace — measuring end-to-end latency

`core/include/deskhub/control/ClockSync.h/.cpp`. The host stamps each frame's capture
time (`VideoHeader::timestampUs`) with *its* clock; the client presents with *its own*
clock. The unknown clock offset cannot be measured directly — every sample
`(client_arrival − host_timestamp)` equals the offset plus that packet's one-way delay.

- **Min filter**: the one-way term is ≥ 0 and varies per packet, so the *minimum* of
  the samples is the best estimate of "offset + smallest achievable one-way delay". A
  new minimum wins immediately; the estimate may only move *up* at a window boundary:
  each `kClockRefreshUs = 10 s` window keeps its own parallel minimum which replaces
  the base when the window closes — this tracks crystal drift in both directions (tens
  of ms per hour on cheap oscillators) without the "e2e snaps to 0 every 10 s" artifact
  a hard reset would cause.
- **`E2eUs(hostTs, localPresent)`** returns `(localPresent − hostTs − offset) + rtt/2`,
  clamped to `[0, u32max]`. Adding back half the RTT restores the one-way delay the min
  filter subtracted — otherwise the fastest frame would always report a beautiful,
  false 0 ms. This is an *estimate* assuming a symmetric path; asymmetric routes
  (Wi-Fi, Tailscale) skew it by exactly the asymmetry. `Rebase` seeds the offset from
  `HelloAck::timebaseUs` so the very first frames already display a number. `OnFrame`
  is fed the *arrival* time, not the present time — the filter should track network
  delay, not local decode/present queueing. (`TestClockSyncOffset` cancels a 1-hour
  clock skew while preserving a 23 ms latency.)
- In the shipping Windows client (`client/windows/cpp/ClientApi.cpp`) the same idea is
  currently implemented inline: `ackDeltaUs` seeded from `HelloAck::timebaseUs`, a
  monotonically minimized RTT from `onRtt`, and
  `e2e = now − (ackDelta − minRtt/2) − timestampUs` computed per rendered frame. The
  `ClockSync` class is the tested core form of this computation.

`core/include/deskhub/control/LatencyTrace.h/.cpp` is the ring buffer behind the
latency sparkline (see 09-diagnostics.md): `kLatencyTraceLen = 60` columns sampled
every `kLatencySampleUs = 320 ms` (~19 s of history — dense enough to catch a single
200 ms stutter, long enough to show a trend). Each bucket keeps the **maximum** sample
seen in its interval, not the last — the chart exists to find the bad moments, and
last-sample would erase a spike landing between marks. Buckets skipped during a stall
are filled with the previous value, never 0 (0 ms would claim a perfect link — the
opposite of what just happened; `TestLatencyTraceGaps`). `Snapshot` returns samples
oldest-to-newest, ready to draw left-to-right.

## 9. Failure modes and recovery, end to end

- **Single loss, FEC on**: rebuilt by `TryRecover` at the client; frame completes, no
  drop, no extra round trip. Counted in `packetsRecovered`.
- **Loss with FEC off, or ≥ 2 losses in one group**: `PlanNack` requests the missing
  fragments after the 2 ms hold; the host answers from the `RetransmitCache`. If the
  retransmit beats the 2-frame-interval deadline, the frame is saved silently.
- **Unrecoverable frame**: dropped (`Timeout`/`Overtaken`/`Evicted`), the barrier
  advances, all subsequent non-IDR frames are swallowed, and the client repeats
  `REQUEST_KEYFRAME` every 250 ms until the forced IDR arrives. Video freezes on the
  last good frame rather than showing reference-corruption smear.
- **Sustained loss**: each Feedback second walks bitrate down ×0.75 or ×0.90 with FEC
  on, settling at the floor (`minBps`). Recovery is deliberately slow: +5% of the
  ceiling per clean second, only after a 2 s cooldown, so the rate converges instead of
  oscillating around the congestion point.
- **Reordering storms**: absorbed by the 4-slot pending map, the ordered-emission rule,
  the 2 ms NACK hold, and the barrier (a late packet can never send the decoder back in
  time). Packets arriving after their frame died are recorded as *late* rather than
  lost, so the stats distinguish a deadline problem from a loss problem.
- **Oversized frames**: more than 65535 fragments — `SendFrame` refuses and sends
  nothing; more than 2048 fragments (`kMaxFecGroups × kFecGroupSize`) — sent plain,
  without FEC.
- **Send-side bursts**: the dominant real-world loss shape (long tail-drop runs from
  IDR bursts) is addressed at the source by the `Pacer` (§5); the burst length of every
  frame send is instrumented on the host for diagnosis.
- **Receiver stalls**: the Windows socket layer raises `SO_RCVBUF` to 4 MB so a brief
  Recv-thread stall does not overflow the kernel buffer — loss there happens before the
  program ever sees the packet, beyond the reach of FEC and NACK alike (see
  `client/windows/cpp/net/UdpSocket.cpp` and 11-platform-transport.md). The 100 ms recv
  timeout keeps `Tick` (ping, feedback, keyframe retry, NACK) running even when the
  peer is silent.
