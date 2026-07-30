# 09 — Diagnostics

Deskhub hunts latency and stutter bottlenecks with a structured, always-on log
stream tagged `[DIAG]`. This document is the reference the code comments point
at ("docs/09"): the line format, how to capture the log on each platform, the
complete event catalog, and how to read the numbers.

Related: 06-transport.md (packetizer/FEC/burst-loss analysis referenced by the
send-side events), 02-agent.md (host pipeline), 03-client.md (viewer pipeline).

## Philosophy

- **Always on.** There is no flag to enable diagnostics and none to disable
  them (`client/windows/cpp/Diag.h`, and the Android note in
  `client/android/app/src/main/cpp/ClientLoop.cpp`: no `DESKHUB_DIAG` flag —
  logcat filters by tag, so `[DIAG]` is simply always emitted). The counters
  run regardless; diag only adds log lines. When something goes wrong, a log
  that already exists is worth far more than a reproduction attempt.
- **One event per line, designed for grep.** The uniform shape is:

  ```
  [DIAG][<source>] evt=<name> k1=v1 k2=v2 ...
  ```

  On the host, `<source>` is the name of the shared display
  (`SourcePipeline::name`), or the literal `agent` for events that belong to
  the receive loop as a whole. Client-side emitters print `[DIAG]` with no
  source component (a viewer shows exactly one source). This is the file users
  attach when asked for remote diagnostics.
- **Never block the hot path.** Counters are windowed atomics
  (`DiagAtomicMax` in `client/windows/cpp/Diag.h` is the shared CAS-max
  helper); printing happens on the receive loop, once per second, or on rare
  events. On Windows, disk I/O is additionally decoupled from the emitting
  thread (see below). The core stays I/O-free: `Reassembler` reports drops
  through the `onFrameDrop` callback and lets each client attach
  printf/logcat.

Two per-second **status lines** accompany the `[DIAG]` stream and are part of
the same diagnosis workflow: `[Agent][<source>] <state> | capture N fps |
send N fps, N kbps | input N (lost N, skipped N) | client loss N%, RTT N ms,
recv N kbps` on the host, and
`[Client] N fps | N kbps | dropped N frame | lost N% pkts | fec+N | RTT N ms |
e2e ~N ms` on the client. The input triple is the input-stage telemetry:
`applied` counts events delivered to the injector, `lost` counts sequence
gaps, and `skipped` counts events yielded because the local user was active
("host wins" — since the foreground gate was removed 2026-07-27 that is the
only skip reason) — the only number that distinguishes "typing does nothing"
from "packets never arrived".

The `client …` tail is everything the host knows about the far end, taken from
the last `FEEDBACK` (~1 s). It reads `client -` until the first one arrives, so
"nothing heard yet" is never mistaken for "0% loss, 0 ms RTT". Added
2026-07-30: before that, link quality reached the host log only through the
`Bitrate X -> Y (loss N%, RTT N ms)` line, which prints **only when the bitrate
changes** — so a link that was bad but *steady* left no trace at all, and
diagnosing it required logs from both machines when users typically send one.

## Capturing the log

### Where the log file lives — all three desktop platforms

**`~/.deskhub/` (2026-07-30).** Windows, macOS and Ubuntu all write to the same
place, so one instruction — *"send me the newest file in `~/.deskhub`"* — is
correct everywhere. `platform/include/deskhubp/LogFile.h` owns the directory and
the file-name convention; the per-platform `Log.h` files only decide how lines
reach it.

- **Path:** `%USERPROFILE%\.deskhub` on Windows, `$HOME/.deskhub` elsewhere
  (falling back to `getpwuid` when `HOME` is unset, as it can be for processes
  launched by launchd or a `.desktop` entry). Created on first use, mode `0700`
  on POSIX — logs carry hostnames, IPs and display names.
- **File name:** `deskhub-<yyyymmdd>-<hhmmss>-<pid>.log`, using **local**
  time (users correlate with the wall clock: "it stuttered around 8:30").
- **One file per process.** The pid keeps concurrent instances apart: on Windows
  sharing-with-control relaunches `Deskhub.exe` under UAC (`runas` verb,
  `client/windows/cpp/ElevatedShare.h`), and on any platform you may run two
  copies to test host↔client. The role (agent/client) is already on every line,
  so it is not encoded in the name.
- **`deskhub-latest.log`** (POSIX only) is a symlink to the current process's
  file, so `tail -f ~/.deskhub/deskhub-latest.log` works without looking up a
  name. Windows has none — creating symlinks there needs a privilege.
- **Hot-path safety:** the file gets a 256 KB full buffer (`_IOFBF`), so a log
  call on the receive/encode path is a memcpy, not a disk write; a detached
  background thread flushes every ~500 ms (at most one flush cycle is lost on a
  crash).
- **Why not next to the executable** (where Windows used to write): the install
  directory is read-only in exactly the most standard installs — Program Files,
  `/Applications` (writing inside a `.app` also breaks its signature),
  `/usr/bin`. The old scheme meant the cleanest installs were the ones with no
  log at all.

Nothing prunes old logs — one file per run accumulates until the user deletes
them.

### Windows

`client/windows/cpp/DiagLog.h` / `DiagLog.cpp` (`StartProcessLog`) redirect
the **entire process output** — every `printf`/`wprintf` on stdout and stderr,
including all `[DIAG]` lines — into that file, from process start until exit.
Redirection (rather than the tee used on Unix) is what lets the hundreds of raw
`printf` call sites stay untouched, and the app has no console to tee to anyway.
There is no checkbox and no console window: the log always exists when you need
to send it. Redirection happens at startup rather than at session start because
failures cluster around negotiation and session setup — enabling logging on
demand would miss exactly the part that matters.

- stderr shares the same file but stays unbuffered so rare errors hit disk
  immediately.
- If the directory or the file cannot be created, `StartProcessLog` returns
  false and the app runs without a log. The first line of a successful log is
  `[DiagLog] <full path> started YYYY-MM-DD HH:MM:SS`.

### Android

All C++ logging goes through logcat with tag `Deskhub`
(`client/android/app/src/main/cpp/Log.h`). Capture with:

```
adb logcat -s Deskhub
```

`-s` filters to the tag and drops every other process's output.

### iOS

`client/ios/app/cpp/Log.h` writes to **stderr** with a `[Deskhub] ` line
prefix (deliberately `fprintf`, not `os_log`, to keep the printf-style call
sites shared with Android). stderr flows into the Xcode debug console, and
into Console.app when running on a device.

iOS and Android deliberately stay off the shared `~/.deskhub/` file: `~` there is
inside the app sandbox, so a file written to it is unreachable for the user who
would have to send it, while the Xcode console and logcat are the channels those
platforms already provide.

### macOS

`client/macos/app/cpp/Log.h` writes every line **twice**, shared by **both
roles** (client and agent): to stderr, and to `~/.deskhub/`. Read it wherever is
convenient —

```
tail -f ~/.deskhub/deskhub-latest.log
```

— or in the Xcode console / the Terminal when the app is launched from the
command line. The file matters because double-clicking `Deskhub.app` sends
stderr nowhere: without it, bug reports from non-developers arrive with no log,
which is precisely when a log is most needed. Teeing (rather than Windows-style
redirection) keeps the Xcode and Terminal workflows intact, and is cheap here
because every line already goes through one function, `deskhubp::LogEmit`.

### Ubuntu

`client/linux/cpp/Log.h` is the same tee, both roles: stderr — the terminal that
launched `deskhub`, or `journalctl --user -f` when the desktop started it — plus
`~/.deskhub/`, same `tail -f` command as macOS.

Two host-side fields exist only on this platform, both on the per-source
`evt=sum` line:

- `zerocopy=1|0` — whether capture negotiated dma-buf (`1`) or fell back to
  copying frames through RAM (`0`). A `0` here explains a capture fps far below
  the requested rate, and is the first thing to check when 4K feels slow
  (17-linux-app.md §6).
- `skipped=` (already present elsewhere) counts input events dropped because
  the person sitting at the machine was typing — on Ubuntu it *also* reads 0
  permanently when `/dev/input/event*` is unreadable, in which case a
  `[HostWins]` warning appears once at session start.

## Event catalog — host side

Emitted by `RunAgent` in `client/windows/cpp/AgentLoop.cpp`,
`client/macos/app/cpp/AgentLoop.cpp` and `client/linux/cpp/AgentLoop.cpp`
(same event names and fields on all three). Per-source events carry
`[DIAG][<source>]`; loop-wide events carry `[DIAG][agent]`.

One reading difference on Ubuntu: `enc_ms_avg`/`enc_ms_max` mean **actual
encode time** there, because VA-API encodes synchronously. On macOS the same
fields only measure how long it took to *hand* the frame to VideoToolbox, so a
near-zero value is normal there and would be suspicious on Ubuntu.

| Stage | Event | Fields | Meaning |
|---|---|---|---|
| Encode | `evt=sum` (per source, 1 s) | `enc_ms_avg`, `enc_ms_max` | Encode wall time over the 1 s window (measured around `IVideoEncoder::Encode` in `SourcePipeline::DiagEncode`): average and worst case in ms. |
| Encode | `evt=sum` (cont.) | `idr` | Number of IDR (key) frames sent in the window. |
| Encode | `evt=enc_fail` | `idr` (0/1), `ms` | An encode call returned failure; `ms` is how long it ran before failing. Catches the previously silent failure on the keepalive/static-IDR path — a dead encoder on a static source means the viewer shows nothing, with no trace. |
| Send | `evt=sum` (cont.) | `burst_ms_max` | Worst per-frame send burst in the window: time from the first to the last UDP packet of one frame (measured around `Packetizer::SendFrame`). |
| Send | `evt=sum` (cont.) | `send_fail` | Count of `sendto` failures in the window (send buffer full, …) — packets lost **at the host** before they ever reach the network. |
| Send | `evt=idr` | `bytes`, `pkts`, `burst_ms` | One IDR frame left the host: encoded size, packet count, and send-burst duration. Recorded on the encode thread, printed on the receive loop to keep I/O off the hot path. IDR size is the single most important host-side number for diagnosing burst loss (06-transport.md §5). |
| Loop health | `evt=sum` (`[agent]`, 1 s) | `loop_busy_ms_max` | Longest single iteration of the receive loop in the window. |
| Loop health | `evt=recv_stall` (`[agent]`) | `busy_ms` | Immediate warning (no 1 s wait) when one receive-loop iteration exceeded 250 ms — while the loop is busy, nobody drains the UDP socket. |

Capture rate itself is on the `[Agent]` status line (`capture N fps` vs
`send N fps`): a healthy capture rate with a sagging send rate points into
encode/send; a sagging capture rate points at the source.

## Event catalog — client side

Emitted by `ClientLoop` in `client/android/app/src/main/cpp/ClientLoop.cpp`,
`client/ios/app/cpp/ClientLoop.cpp`, `client/macos/app/cpp/ClientLoop.cpp`,
and `client/windows/cpp/ClientApi.cpp` (identical names and fields). The
Windows viewer emitted none of these until 2026-07-30 — which is exactly why
a constant latency floor sitting in its presentation path went unattributed
for so long. It is the only viewer with a `present_ms` field, because it is
the only one that owns its swap chain (see the e2e section below).

| Stage | Event | Fields | Meaning |
|---|---|---|---|
| Receive/assemble | `evt=frame_drop` | `id`, `reason`, `miss=<missing>/<total>`, `pos`, `idr` (0/1), `waited_ms`, `got_bytes` | Autopsy of one frame the `Reassembler` gave up on (`Reassembler::FrameDropInfo`, `core/include/deskhub/transport/Reassembler.h`). `reason` ∈ `timeout` (2 frame intervals passed, pieces still missing), `overtaken` (≥2 newer complete frames passed it), `evicted` (pending queue full), `pre_idr` (intact frame swallowed while waiting for an IDR — not packet loss). `pos` places the missing run: `head`/`tail`/`mid`/`all`, or `-` when nothing is missing; `tail` is the signature of burst loss (06-transport.md §5). `waited_ms` = first piece → drop; `got_bytes` = bytes that did arrive. |
| Receive/assemble | `evt=sum` (1 s) | `asm_ms=<avg>/<max>` | Assembly time per completed frame: first piece arrived → frame complete (`Reassembler::Frame::firstSeenUs`). |
| Receive/assemble | `evt=sum` (cont.) | `late`, `late_ms_avg`, `late_ms_max` | Packets that arrived **after** their frame was already dropped as "lost" (`Reassembler::Stats::latePackets` via `LinkWindow`). This is the arbiter of "real loss vs late arrival": if `late` accounts for most of the loss, the packets exist — the reassembly deadline just expires before the tail arrives. |
| Receive/assemble | `evt=sum` (cont.) | `gap_ms_max` | Longest silence between two consecutive video packets in the window (`Reassembler::TakeMaxGapMs`). Gaps of ~100+ ms point at Wi-Fi congestion/power-save: packets bunch up somewhere and arrive in a clump. |
| Decode | `evt=sum` (cont.) | `dec_ms=<avg>/<max>` | Decode(+enqueue) wall time per frame, measured on the decode thread. On Windows this **includes** blit and `Present`, which run synchronously inside `Decode` — subtract `present_ms` to isolate the MFT. |
| Present | `evt=sum` (cont., **Windows only**) | `present_ms=<avg>/<max>` | Time blocked inside `IDXGISwapChain::Present` per frame (`PanelRenderer::RenderNV12`). This is queueing for the display, not transit, so it is deliberately **excluded** from `e2e`. A value near one refresh interval (~16 ms at 60 Hz) is normal and is the floor a non-tearing presenter can reach; values several times that mean the DXGI present queue is backed up. |
| Decode | `evt=sum` (cont.) | `dq_drop` | Frames discarded because the decode queue was full (the oldest frame is dropped, never the newest) — the decoder is not keeping up with the network. |
| Decode | `evt=sum` (cont., **Apple + Android**) | `disp_drop` | Frames dropped because the *display* stage was backed up: `AVSampleBufferDisplayLayer.isReadyForMoreMediaData` was false, or MediaCodec would not hand out an input buffer in time. **On the Apple viewers this is the only counter that can ever show congestion** — `enqueueSampleBuffer` is asynchronous, so the decode thread always drains `decQueue_` instantly and `dq_drop` stays at 0 no matter how badly the layer is backed up. A non-zero `disp_drop` is always paired with a `kf_req reason=display_congested`, because dropping a frame breaks the inter-frame chain. |
| Recovery | `evt=kf_req` | `reason` | The client started asking the host for a keyframe. `reason` ∈ `loss` (`Reassembler::TakeLossEvent`), `wait_idr` (still swallowing frames until an IDR arrives), `dec_fail` (decoder failed **and was torn down**), `q_overflow` (decode queue overflowed), `display_congested` (the display/codec was backed up and a frame was dropped — the decoder is **fine**, only busy; Apple and Android viewers only). Logged only on the transition into the pending state, however often the request is re-sent. |
| Recovery | `evt=idr_rx` | `bytes`, `after_ms` | The requested IDR arrived: its size and the time since the matching `kf_req` — the picture-recovery time the user actually experienced. |
| Loop health | `evt=sum` (cont.) | `loop_busy_ms_max` | Longest net-loop iteration in the window. |
| Loop health | `evt=recv_stall` | `busy_ms` | Immediate warning when one net-loop iteration exceeded 50 ms. While the loop is stalled the kernel UDP buffer is the only slack — overflow there is real, self-inflicted packet loss. |
| Latency | `evt=mft_ts_drift` (**Windows only**, once per decoder) | `input_us`, `mft_us`, `diff_ms` | The Media Foundation decoder handed back an output sample time more than 100 ms away from the input time it was given. Printed once, purely to confirm on real hardware whether that MFT synthesises timestamps from `MF_MT_FRAME_RATE`/`SampleDuration` instead of copying them. The viewer does not depend on the answer — it uses the input timestamp regardless (`MfDecoder::Deliver`) — but seeing this line means the old behaviour would have made `e2e_ms` drift upward on a static screen. |
| Latency | `evt=sum` (cont.) | `min_rtt_ms`, `e2e_ms` | The **input and the output** of the e2e estimate below. Without them the `e2e ~N ms` on the `[Client]` line cannot be checked against anything. Note `min_rtt_ms` is the minimum-ever RTT — *not* the `RTT` printed on the `[Client]` line, which is the most recent sample. The pure queueing term is `e2e_ms − min_rtt_ms/2`; that subtraction is the single most useful number on the line. `e2e_ms = -0.0` means no sample yet, not zero latency. |

## End-to-end latency

**Which stages carry timestamps.** The host stamps each frame with its clock
at the moment the frame is handed to the encoder (`SourcePipeline::DiagEncode`
passes `NowUs()` into `Encode`; the value travels on the wire as
`VideoHeader::timestampUs`). `HelloAck::timebaseUs` carries the host clock at
negotiation. On the client, the `Reassembler` records `firstSeenUs` per frame
(feeds `asm_ms`), the decode thread measures around `Decode` (feeds
`dec_ms`), and RTT comes from the session ping/pong (`onRtt` callback /
`lastRttUs`).

**How e2e is computed.** The two machines' clocks share no epoch
(`NowUs()` is monotonic-since-boot on both, `platform/include/deskhubp/Clock.h`),
so the unknown constant `C = client clock − host clock` has to be cancelled
before any subtraction means anything. All five viewers do it with one shared
implementation — `deskhub::ClockOffset`
(`core/include/deskhub/control/ClockOffset.h`), a **sliding-window min filter**
over the one-way delay:

```
raw    = client clock when the frame reached its destination − frame timestampUs
       = C + (that frame's true latency)          // C is constant, so:
floor  = min(raw) over a sliding 10 s window      // = C + best latency observed
e2e    = (raw − floor)  +  minRTT/2
          └── exact ──┘     └─ measured floor added back ─┘
```

`raw − floor` is **exact**: `C` cancels completely, no clock sync is assumed,
no path symmetry is assumed, and the result is always ≥ 0. It is the *queueing*
delay — the part that varies, and the only part anything can be done about.
`minRTT/2` adds back the one piece of the floor that *is* measurable
independently (RTT needs no clock sync — the host echoes the client's own
stamp). This is not double-counting: the min filter removed the floor, and this
adds back the measured share of it.

What it still misses is the rest of the floor: best-case encode, and best-case
decode/present. Those are not guessed at — they are measured separately and
printed as `enc_ms` (host), `dec_ms` and `present_ms` (client), so a full
picture is a sum across two log lines rather than a single fabricated number.

The window is 10 s and rotates through two buckets, so the floor is **re-learnt
within 10–20 s** when the path degrades mid-session (Wi-Fi roam, a VPN dropping
to a relay). A min-since-session-start never forgets, and would report every
frame after such a change as permanently late.

**What this replaced, and why** (2026-07-30). Until this change the offset came
from a single sample: `ackDeltaUs = client clock at HELLO_ACK −
HelloAck::timebaseUs`, minus `minRTT/2`. That is the only host clock value on
the wire, it was captured once, and it was never refined. It is also the *first
packet exchange of the session* — systematically the slowest one (ARP/ND
resolution, Wi-Fi waking from power-save, firewall first-packet, Tailscale still
on DERP before it upgrades to a direct path). The residual bias was
`minRTT/2 − (one-way delay of that ACK)`: unknown magnitude, usually negative
(so e2e read low), fixed for the whole session, and invisible in any log. Better
a number that is short by a floor you know the shape of than a number that is
wrong by an amount nobody can see. `HelloAck::timebaseUs` is still on the wire
and still sent; no viewer reads it any more.

**⚠ Where each viewer stops the clock — read this before comparing two
platforms.** `now` above is not the same instant everywhere, and the gap is
large enough to look like a network problem:

| Viewer | Clock stops at | Excluded from the number |
|---|---|---|
| iOS / macOS | `enqueueSampleBuffer` into `AVSampleBufferDisplayLayer` (`VtDecoder::lastRenderedPtsUs`) | the decode itself, and the layer's presentation delay — there is no per-frame "on screen" callback to hook |
| Android | `AMediaCodec_releaseOutputBuffer(…, true)` — frame handed to the `Surface` (`MediaCodecDecoder::lastRenderedPtsUs`) | compositor presentation |
| Ubuntu | the GL draw actually completing (`VideoRenderer::lastRenderedPtsUs`) | compositor presentation |
| Windows | after `VideoProcessorBlt`, **before** `Present` (`PanelRenderer::RenderNV12`, `outReadyUs`) | the `Present` block only, reported separately as `present_ms` |

The Apple number is therefore structurally optimistic, and until 2026-07-30
the Windows number was structurally pessimistic — it stopped the clock *after*
`Present`, so a stalled DXGI present queue was charged to the link. Comparing
"iOS is fine but Windows is terrible" across those two definitions is not a
valid comparison; compare `present_ms` and `dec_ms` instead.

All five viewers now stop the clock **at the moment the frame is dealt with**, on
the decode thread. Ubuntu was the exception until 2026-07-30: it computed e2e
inside the 1 s stats block, as `now − offset − (PTS of the last frame drawn)`.
Those are two different instants, so the number carried however long ago that
last frame was drawn — on a *static* source, where the host only sends a
keepalive at ~2 fps, that inflated e2e by up to 500 ms. The stiller the screen,
the worse the reported latency, which is precisely backwards.

**Known limits of the estimate.** Two remain, both bounded and both known-signed
— which is the whole point of the design:

- **The floor is excluded.** `e2e` reports queueing plus `minRTT/2`, so it is
  short by the best-case encode + decode + present. Read `enc_ms`, `dec_ms` and
  `present_ms` alongside it; those cover the missing terms. An absolute number
  that includes the floor is mathematically unavailable without a synchronised
  clock — the only honest options are "short by a known amount" or "wrong by an
  unknown one".
- **`min_rtt` never decays.** It is the minimum ever seen, so a path that
  degrades mid-session leaves the *added-back floor term* anchored to the old
  path. The error is bounded by `minRTT/2` and only affects that one term — the
  queueing part is unaffected, because `ClockOffset` has its own sliding window.

`Feedback::rttMs` — the only RTT the **host** ever sees — is a `uint16` in whole
milliseconds, so a sub-millisecond wired-LAN RTT rounds to 0 or 1. The client
keeps microseconds internally (`ClientSession::lastRttUs`); only the number
crossing to the host is that coarse. It rounds rather than truncates: truncation
turned every sub-millisecond LAN into a permanent `RTT 0 ms` on the host, which
reads as "no data" rather than "very fast".

**Reading the numbers.** `e2e_ms` near `min_rtt_ms/2` means there is no queueing
anywhere — the pipeline is at its floor and nothing in the viewer will improve
it. `e2e_ms` well above that is queueing, and the other fields on the same line
say where: `asm_ms` (waiting for packets), `dq_drop` (decoder behind the
network), `present_ms` (display, Windows only), `gap_ms_max` (packets arriving
in clumps).

That presentation floor is a real cost, not just a measurement artifact.
`PanelRenderer` therefore sets `IDXGIDevice1::SetMaximumFrameLatency(1)` (DXGI
defaults to **three** queued frames — ~50 ms at 60 Hz, constant, present from
the very first frame) and uses `DXGI_SWAP_EFFECT_FLIP_DISCARD` so a stale
frame is dropped rather than shown ahead of a newer one.

(The dedicated
`ClockSync`/`LatencyTrace` classes in core — a drift-tracking min filter and
the sparkline ring buffer — were removed 2026-07-27: no client ever wired
them in, and this inline estimate is what ships.)

## How to diagnose: localizing a bottleneck

Compare stages left to right; the first stage whose number explodes owns the
problem.

1. **Host encode?** Check `enc_ms_avg`/`enc_ms_max` in the host `evt=sum`.
   Max spikes over a normal average = periodic stutter at the encoder;
   `evt=enc_fail` = the encoder is failing outright (expect a blank viewer
   on a static source).
2. **Host send?** `send_fail > 0` means packets died in the host's own send
   buffer. High `burst_ms_max`, and `evt=idr` with large `bytes`/`pkts`,
   mean big frames leave as long bursts — correlate with client
   `frame_drop … pos=tail` to confirm burst loss (06-transport.md §5).
3. **Network?** Client `evt=frame_drop` plus the loss fields of the
   `[Client]` status line. Then split "real loss vs late": if `late` in the
   client `evt=sum` covers most of the loss, packets arrive after the
   reassembly deadline (queueing) rather than disappearing. `gap_ms_max` in
   the hundreds of ms points at Wi-Fi buffering/power-save.
4. **Client receive loop?** Client `evt=recv_stall` / `loop_busy_ms_max`:
   the viewer itself is stalling its socket, causing kernel-buffer loss that
   looks exactly like network loss.
5. **Client decode?** `dec_ms` avg/max and `dq_drop` in the client
   `evt=sum`; `kf_req reason=dec_fail|q_overflow` names the decoder as the
   trigger of a keyframe request.
6. **Recovery quality.** For every visible freeze, pair `evt=kf_req` with
   the following `evt=idr_rx`: `after_ms` is how long the user stared at a
   stale frame, and `bytes` (vs the host's `evt=idr`) confirms which IDR
   healed it.
7. **Input feels dead?** On the host `[Agent]` status line, distinguish
   `input … lost` (events never arrived — network) from `skipped` (arrived,
   but yielded to the person at the host machine — "host wins").

Agent `recv_stall` fires above 250 ms, client above 50 ms — the client loop
also paces decode and input, so it is held to a tighter budget.
