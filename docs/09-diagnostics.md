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
send N fps, N kbps | input N (lost N, skipped N)` on the host, and
`[Client] N fps | N kbps | dropped N frame | lost N% pkts | fec+N | RTT N ms |
e2e ~N ms` on the client. The input triple is the input-stage telemetry:
`applied` counts events delivered to the injector, `lost` counts sequence
gaps, and `skipped` counts events yielded because the local user was active
("host wins" — since the foreground gate was removed 2026-07-27 that is the
only skip reason) — the only number that distinguishes "typing does nothing"
from "packets never arrived".

## Capturing the log

### Windows

`client/windows/cpp/DiagLog.h` / `DiagLog.cpp` (`StartProcessLog`) redirect
the **entire process output** — every `printf`/`wprintf` on stdout and stderr,
including all `[DIAG]` lines — into one file next to the executable, from
process start until exit. There is no checkbox and no console window anymore:
the log always exists when you need to send it. Redirection happens at
startup rather than at session start because failures cluster around
negotiation and session setup — enabling logging on demand would miss exactly
the part that matters.

- **File name:** `deskhub-<yyyymmdd>-<hhmmss>-<pid>.log`, using **local**
  time (users correlate with the wall clock: "it stuttered around 8:30").
- **One file per process.** The pid keeps the normal instance and the
  elevated instance apart when both start within the same second: sharing
  with control relaunches `Deskhub.exe` under UAC (`runas` verb) with a
  `--share` command line (`client/windows/cpp/ElevatedShare.h`, driven by
  the Win32 UI), so two processes may be logging at
  once. The role (agent/client) is already on every line, so it is not
  encoded in the name.
- **Hot-path safety:** stdout gets a 256 KB full buffer (`_IOFBF`), so a
  `printf` on the receive/encode path is a memcpy, not a disk write; a
  detached background thread flushes every ~500 ms (at most one flush cycle
  is lost on a crash). stderr shares the same file but stays unbuffered so
  rare errors hit disk immediately.
- If the file cannot be created (read-only directory, e.g. the exe under
  Program Files), `StartProcessLog` returns false and the app runs without a
  log. The first line of a successful log is
  `[DiagLog] <name> started YYYY-MM-DD HH:MM:SS`.

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

### macOS

`client/macos/app/cpp/Log.h` is the same stderr mechanism, shared by **both
roles** (client and agent). Read it in the Xcode console, or in the Terminal
when the app is launched from the command line.

### Ubuntu

`client/linux/cpp/Log.h` is again the same stderr mechanism, both roles. Read
it in the terminal that launched `deskhub`, or with
`journalctl --user -f` when the desktop started it.

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
`client/ios/app/cpp/ClientLoop.cpp`, and
`client/macos/app/cpp/ClientLoop.cpp` (identical names and fields).
The Windows viewer (`client/windows/cpp/ClientApi.cpp`, headless) computes
the same stats and e2e estimate but currently emits **no** client-side
`[DIAG]` lines.

| Stage | Event | Fields | Meaning |
|---|---|---|---|
| Receive/assemble | `evt=frame_drop` | `id`, `reason`, `miss=<missing>/<total>`, `pos`, `idr` (0/1), `waited_ms`, `got_bytes` | Autopsy of one frame the `Reassembler` gave up on (`Reassembler::FrameDropInfo`, `core/include/deskhub/transport/Reassembler.h`). `reason` ∈ `timeout` (2 frame intervals passed, pieces still missing), `overtaken` (≥2 newer complete frames passed it), `evicted` (pending queue full), `pre_idr` (intact frame swallowed while waiting for an IDR — not packet loss). `pos` places the missing run: `head`/`tail`/`mid`/`all`, or `-` when nothing is missing; `tail` is the signature of burst loss (06-transport.md §5). `waited_ms` = first piece → drop; `got_bytes` = bytes that did arrive. |
| Receive/assemble | `evt=sum` (1 s) | `asm_ms=<avg>/<max>` | Assembly time per completed frame: first piece arrived → frame complete (`Reassembler::Frame::firstSeenUs`). |
| Receive/assemble | `evt=sum` (cont.) | `late`, `late_ms_avg`, `late_ms_max` | Packets that arrived **after** their frame was already dropped as "lost" (`Reassembler::Stats::latePackets` via `LinkWindow`). This is the arbiter of "real loss vs late arrival": if `late` accounts for most of the loss, the packets exist — the reassembly deadline just expires before the tail arrives. |
| Receive/assemble | `evt=sum` (cont.) | `gap_ms_max` | Longest silence between two consecutive video packets in the window (`Reassembler::TakeMaxGapMs`). Gaps of ~100+ ms point at Wi-Fi congestion/power-save: packets bunch up somewhere and arrive in a clump. |
| Decode | `evt=sum` (cont.) | `dec_ms=<avg>/<max>` | Decode(+enqueue) wall time per frame, measured on the decode thread. |
| Decode | `evt=sum` (cont.) | `dq_drop` | Frames discarded because the decode queue was full (the oldest frame is dropped, never the newest) — the decoder is not keeping up with the network. |
| Recovery | `evt=kf_req` | `reason` | The client started asking the host for a keyframe. `reason` ∈ `loss` (`Reassembler::TakeLossEvent`), `wait_idr` (still swallowing frames until an IDR arrives), `dec_fail` (decoder failed and was torn down), `q_overflow` (decode queue overflowed). Logged only on the transition into the pending state, however often the request is re-sent. |
| Recovery | `evt=idr_rx` | `bytes`, `after_ms` | The requested IDR arrived: its size and the time since the matching `kf_req` — the picture-recovery time the user actually experienced. |
| Loop health | `evt=sum` (cont.) | `loop_busy_ms_max` | Longest net-loop iteration in the window. |
| Loop health | `evt=recv_stall` | `busy_ms` | Immediate warning when one net-loop iteration exceeded 50 ms. While the loop is stalled the kernel UDP buffer is the only slack — overflow there is real, self-inflicted packet loss. |

## End-to-end latency

**Which stages carry timestamps.** The host stamps each frame with its clock
at the moment the frame is handed to the encoder (`SourcePipeline::DiagEncode`
passes `NowUs()` into `Encode`; the value travels on the wire as
`VideoHeader::timestampUs`). `HelloAck::timebaseUs` carries the host clock at
negotiation. On the client, the `Reassembler` records `firstSeenUs` per frame
(feeds `asm_ms`), the decode thread measures around `Decode` (feeds
`dec_ms`), and RTT comes from the session ping/pong (`onRtt` callback /
`lastRttUs`).

**How e2e is computed** (identical in all four viewer loops — e.g.
`ClientLoop::DecodeThread` on macOS and the mirrored block in
`client/windows/cpp/ClientApi.cpp`): the two machines' clocks are not
synchronized, so the client estimates the offset —

```
ackDeltaUs = client clock at HELLO_ACK − HelloAck::timebaseUs
offset     = ackDeltaUs − minRTT/2        // minimum RTT ever seen
e2e        = now − offset − frame timestampUs
```

The minimum RTT is used because the smallest sample is the least polluted by
queueing. Known limit: this **assumes a symmetric path**; asymmetry (common
on Wi-Fi and Tailscale) shifts the number by exactly the asymmetric part.
Treat e2e as an estimate of "host capture → client display", shown on the
`[Client]` status line and the on-screen overlay. (The dedicated
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
