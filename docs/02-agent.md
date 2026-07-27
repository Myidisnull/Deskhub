# 02 — Agent (Host) Role

The Agent is the sharing side of Deskhub: it captures one or more displays (whole monitors —
per-window sharing was removed 2026-07-27), encodes them
as H.264, packetizes and sends the stream over UDP, and injects the viewer's input back into the
machine. Both desktop apps contain the Agent and the Client role side by side; this document covers
the Agent. System overview: `01-architecture.md`. The viewer side: `03-client.md`.

There are two full implementations of the same orchestration:

| | Windows | macOS |
|---|---|---|
| Orchestrator | `client/windows/cpp/AgentLoop.cpp` (`RunAgent`) | `client/macos/app/cpp/AgentLoop.cpp` (`AgentLoop` class) |
| Capture | Windows Graphics Capture (`capture/ScreenCapture`) | ScreenCaptureKit (`agent/ScreenCapture`) |
| Encode | NVENC → Media Foundation (`encode/EncoderFactory`) | VideoToolbox (`agent/VtEncoder`) |
| Inject | `SendInput` (`input/InputInjector`) | CGEvent (`agent/InputInjector`) |
| UI frontend | Win32 app (`SessionWindow` drives `AgentLoop` directly) | SwiftUI via `AgentLoop` methods |

Session state, packet formats and congestion policy are platform-neutral
and live in `core/` (`deskhub::HostSession`, `deskhub::Beacon`,
`deskhub::BitrateController`, `deskhub::Packetizer`). The agent loops own only the OS-specific
parts: sockets, threads, GPU, capture and injection.

## 1. Entry points

**Windows.** The Win32 app (`client/windows/win32/`) — the only Windows frontend since the WinUI3
and ImGui apps were removed 2026-07-27 — drives `RunAgent` directly with a
`SessionWindow` as its `AgentControl`: it copies the
selected sources, spawns a background thread, calls `capture::InitRuntime()` (WinRT MTA — WGC
requires it on the thread that creates `ScreenCapture`), then calls `RunAgent`, which blocks until
the session ends. `RunAgent(sources, opt, ctl)` takes:

- `AgentOptions` (`AgentLoop.h`): `fps` (60) and `bitrateMbps` (20) — that is all. There is no
  `port` (it is the constant `kDeskhubPort` = 47777 in `net/UdpSocket.h`; a busy port is a hard
  error, the host never walks to another one) and no `allowInput` (mouse and keyboard are always
  shared). Both were removed 2026-07-27 when the app narrowed to plain remote desktop.
- `AgentControl&` (`AgentControl.h`) — the abstract frontend interface: `active()`,
  `stopRequested()`, `SetRows()`, `OnBound()`, `OnFailed()`.
  The Win32 app implements it as `SessionWindow` (a small always-on-top session window).
  `SessionRow.h` (`SessionSourceRow`) is the row type `SetRows`
  pushes to the UI (per-source name, size, viewer address, fps/kbps/RTT, HMONITOR key).

`sources` is the **final** list, not an initial one. Pressing Share hands over *every*
attached display, and the roster cannot change afterwards — the Add / Stop-selected buttons and
their `TakeAdds`/`TakeRemoves` channel were removed 2026-07-27, along with the source picker
itself. Plugging in a monitor mid-session means stopping and sharing again. The only thing
still flowing UI → Recv is `stopRequested()`. If `RunAgent` exits on its own (port busy, GPU
init failure, socket error), the UI is told via `OnFailed` so it can leave the "sharing" state.

**macOS.** SwiftUI cannot be blocked, so `agent/AgentLoop.h` exposes a class instead:
`Start(sources, opt)` binds the port, starts the pipelines, launches the Recv thread and returns
(`Start` itself still blocks a few seconds waiting for first frames — call it off the main thread).
The UI polls `Status()`/`StatusLine()`; the only command it can issue is `Stop`. `Start` refuses
to run without the Screen Recording permission (`macperm::HasScreenRecording`).

## 2. The main loop

Thread architecture (documented in detail at the top of each `AgentLoop.cpp` — read that header
before touching either file):

- **Per source, one hot path** producing encoded frames. Windows: the WGC `FrameArrived`
  thread-pool callback runs capture → encode → `onPacket` → `deskhub::Packetizer::SendFrame` →
  `UdpSocket::SendTo`. macOS: the SCStream queue submits to VideoToolbox, which delivers compressed
  frames asynchronously on its own thread; `VtEncoder::emitMutex_` serializes `onPacket` so the
  single-threaded Packetizer stays safe.
- **One shared Recv thread** for all sources: `recvfrom` with a 100 ms timeout, packet routing,
  `HostSession::Tick` for every source, 1 s statistics window, and the UI row publish.

So N sources means N+1 threads on Windows; every field crossing that boundary in `SourcePipeline`
is atomic or mutex-protected, and each field's owner thread is annotated in the struct.

**Packet routing** (one socket, many sources): pre-session queries (LIST_SOURCES, probe PING) are answered first (see §7);
`HELLO` is routed by `hello.sourceId` (no sessionId exists yet); everything else is routed by
matching `sessionId` against each source's `HostSession`. A valid in-session packet also updates the
stored peer address (`peerPacked`), which is how a client that roams to a new IP/port keeps its
session.

**HostSession drives state.** The loop never interprets control messages itself. It hands every
routed packet to `deskhub::HostSession::HandlePacket` and reacts through `HostCallbacks`: `onStart`
(set the `forceIdr` atomic), `onKeyframeRequest` (same), `onInput` → `InputInjector::Apply`,
`onNack` → replay datagrams from `deskhub::RetransmitCache`, `onFocus` (only
`focused=false` matters now: `InputInjector::ReleaseAll` — the raise-to-foreground
action went with window sharing, removed 2026-07-27),
`onFeedback` → `deskhub::BitrateController` (adjusts encoder bitrate via `SetBitrate`, toggles FEC,
floor 1 Mbps), `onDisconnect` → clear peer, `ReleaseAll`, reset the retransmit cache. `forceIdr` is
an atomic flag on purpose: it is set on the Recv thread and consumed by the next `Encode` on the
capture-side thread — the encoder must never be called from the Recv thread directly.

**Startup phases of `RunAgent`** (mirrored by `AgentLoop::Start` on macOS): validate input (max
`deskhub::kMaxSources` = 8 sources), create the shared D3D11 device (`GpuSelect`), open the UDP
socket — if the requested port is busy it probes up to 64 consecutive ports and reports the real
bound port via `OnBound`; ensure the firewall rule (§7); start capture per source; wait up to 10 s
for each source's first frame (the offer in `HELLO_ACK` needs real dimensions — sources that never
produce a frame are dropped without killing the session); build `HostSession` + `InputInjector` per
surviving source; then enter the Recv loop.

**Static-source handling.** Both capture APIs only deliver frames when content changes, so each
pipeline caches the last frame (Windows: a copied D3D11 texture; macOS: a retained
`CVPixelBufferRef`). The Recv loop re-encodes the cached frame when (a) an IDR request has been
pending >200 ms with no new frame — otherwise a client joining a static screen stays black forever —
or (b) as a ~2 fps keepalive after 500 ms of silence, which flushes async encoders (QSV MFTs hold
the last output until the next input).

**Resize and minimum size.** A size change on the hot path tears down the encoder and cache and
sets `sizeChanged`; the Recv loop then updates the offer (`HostSession::SetOffer`), sends `RECONFIG`
and forces an IDR. Sources smaller than 160×64 (`kMinEncodeW/H` — hardware encoders reject tiny
frames) enter a reversible `paused` state, distinct from the one-way `failed` state, so a display
that momentarily reports a degenerate mode and later restores resumes streaming.

**Pacing.** `client/windows/cpp/net/Pacer.h/.cpp` implements a credit-clock pacer with a
high-resolution waitable timer, built to spread IDR bursts that overflow bottleneck queues (its
header documents the measured burst-loss shape that motivated it). **It is currently not wired into
the send path**: `Packetizer::SendFrame` calls `sendto` directly from the encode-side thread, where
sleeping is forbidden (it would stall the WGC frame pool — a failure mode the Pacer header itself
warns about; pacing requires a dedicated send thread that does not exist yet). Burst duration is
instead measured and logged (`dgBurstMsMax`, IDR burst events) per `09-diagnostics.md`. Packetizer,
FEC, NACK/retransmit details: `04-protocol.md` and `06-transport.md`.

## 3. Sources: enumeration and selection

Only displays can be shared (per-window sharing, `WindowFinder`, and the wire-level
`SourceInfo.kind` byte were all removed 2026-07-27):

- **Windows** — `capture/DisplayFinder.h`: `ListDisplays()` is the single source of
  enumeration; it returns `HMONITOR`s with synthetic names ("Display 1 (primary)"),
  primary first.
- **macOS** — `agent/SourceEnum.h`: `GetShareSources()` asks `SCShareableContent` (the same source
  of truth ScreenCaptureKit captures from) and lists its `SCDisplay`s only. Blocks ~2 s; returns
  an empty list when Screen Recording permission is missing.

An `AgentSource` is just a display handle plus its name (`HMONITOR` on Windows,
`CGDirectDisplayID` on macOS); `deskhub::SourceInfo` carries {sourceId, width, height,
name}. Source ids are assigned incrementally and
**never reused** within a run, so a client holding a stale `SOURCE_LIST` cannot HELLO into the
wrong source. Sources added mid-session go through a `pendingAdds` list with a 10 s first-frame
deadline before `attachSession` promotes them.

**Capture backends.** Windows uses Windows Graphics Capture: event-driven `FrameArrived` delivering
BGRA D3D11 textures that are only valid inside the callback (`capture/CaptureTypes.h`). macOS uses
ScreenCaptureKit configured for NV12 (`'420v'`) so VideoToolbox needs no color conversion; only
`SCFrameStatusComplete` frames are processed, sizes are rounded down to even, and a 500 ms watcher
in `ScreenCapture.mm` calls `updateConfiguration` on source resize because SCStream never resizes
its buffers on its own.

## 4. Encoders

**GPU selection (Windows).** `capture/GpuSelect.h` creates **one** D3D11 device shared by capture
and encode (preference NVIDIA → Intel → AMD, falling back to WARP software rendering) — textures
must never cross devices or every frame takes a round trip through system memory.

**Backend selection (Windows).** `encode/EncoderFactory.cpp` tries backends in order and returns
the first whose `Init` succeeds: 1) `NvencEncoder`, 2) `MfEncoder` (Media Foundation, which itself
picks a hardware MFT for the device — Intel QSV, AMD — or software). No capability probing: `Init`
failing is the reliable signal. Both implement `encode/IVideoEncoder.h`: D3D11 texture in, Annex-B
NAL out via a synchronous `onPacket`, plus `SetBitrate` for mid-stream congestion control without
an encoder rebuild. `EncoderConfig` carries both the even encode size and the true (possibly odd)
texture size so the video processor crops instead of scaling.

**IDR on demand.** NVENC is configured with `NVENC_INFINITE_GOPLENGTH` and `repeatSPSPPS = 1`:
no periodic keyframes; an IDR (with SPS/PPS attached) is emitted only when `Encode` is called with
`forceKeyframe` (`NV_ENC_PIC_FLAG_FORCEIDR | OUTPUT_SPSPPS`). `MfEncoder` requests keyframes via
`ICodecAPI` where supported and falls back to recreating the transform on drivers that ignore it
(see `MfEncoder.cpp`). The request originates as the `forceIdr` atomic set by `onStart`,
`onKeyframeRequest`, or a resize. Note: `HostSession` parses `INVALIDATE_REF` and offers an
`onInvalidateRef` callback, but neither agent loop wires it — hosts currently ignore that message
and recover via NACK retransmit or a requested IDR.

**macOS.** `agent/VtEncoder` is the single backend (no abstract interface — VideoToolbox runs on
every Mac). Configured RealTime, no frame reordering, infinite GOP with IDR on demand, CBR-ish via
data-rate limits. Its `.mm` converts VideoToolbox's AVCC output to Annex-B and prepends SPS/PPS to
every IDR, matching the NVENC `repeatSPSPPS` contract that mid-stream joiners depend on.

## 5. Session lifecycle and security

`deskhub::HostSession` (`core/include/deskhub/session/HostSession.h`) is a per-source state
machine: `Idle → Ready → Streaming`. One client per source-session (v1): a HELLO
from a different `clientId` while Ready/Streaming is rejected with `HELLO_ACK codec=Rejected,
reason=Busy`. A HELLO without H.264 in its codec mask is rejected with `CodecMismatch`. Rejects
reuse the `HELLO_ACK` message so clients get an immediate, reasoned answer instead of a timeout.

**No password gate.** The auth layer (GĐ10) was removed 2026-07-27 (trusted-LAN
decision, 15-review-todo.md §A1): every HELLO goes straight through, and nothing on
the wire is encrypted — keep the host port inside trusted networks.

**Session integrity.** Session ids come from the OS CSPRNG via the `randomBytes` callback
(`deskhubp::RandomBytes` — core cannot touch the OS). If entropy is unavailable the host **fails
closed**: it rejects the connection rather than fall back to a guessable id or a fixed nonce.
`InSession()` additionally requires `sessionId != 0`, closing the window where a forged
`sessionId=0` START during handshake would jump straight to Streaming.

**Timeouts and teardown.** Every valid in-session packet feeds `lastRecvUs_`;
`HostSession::Tick` disconnects after 5 s of silence (`kSessionTimeoutUs`). `BYE` disconnects
immediately (and deliberately returns `false` so the loop does not update the peer from it). On
disconnect the loop clears the peer, calls `InputInjector::ReleaseAll` (a client that vanishes
mid-keypress must not leave keys stuck), and resets the retransmit cache. Shutting a source down
(`shutdownPipeline`) sends a courtesy `BYE`, stops capture, finishes the encoder, and is idempotent.

## 6. Input on the host side

Injection details live in `07-input.md`; the agent-loop contract is:

- `cb.onInput` hands sanitized, in-order `deskhub::InputEvent`s (via `deskhub::InputReceiver`
  inside `HostSession`) to `InputInjector::Apply` on the Recv thread. There is no gate in front
  of this: `HostSession` always accepts `INPUT_EVENT` (the `SetInputAllowed` switch and the
  `kAckFlagInputAccepted` bit in HELLO_ACK were both removed 2026-07-27).
- **No foreground gate.** With whole displays as the only source kind there is no "other app"
  outside what is shared, so the old foreground gate (`TargetHasFocus`/`FocusTarget`, which
  dropped or redirected input when the shared window was not foreground) was removed 2026-07-27
  along with window sharing. Two safety mechanisms remain: `ReleaseAll` (stuck-key prevention —
  triggered by `SET_FOCUS(false)`, BYE, session timeout, and shutdown) and "host wins"
  (`LocalInputMonitor`, next bullet). `SET_FOCUS(true)` requires no host action; `skipped()` now
  counts only events yielded to the local user.
- **`LocalInputMonitor` ("host wins").** Watches *physical* mouse/keyboard activity (Windows:
  low-level hooks on a dedicated message-pump thread, filtering `LLKHF_INJECTED`; macOS: an NSEvent
  global monitor filtering events stamped with Deskhub's `kCGEventSourceUserData` marker). While
  the local user is active, remote input yields for ~1 s — preventing cursor tug-of-war and
  cross-contaminated modifiers. Always started — every session accepts input.

## 7. Windows specifics

- **`ElevatedShare.h` / UAC.** UIPI silently swallows `SendInput` aimed at higher-integrity
  processes (admin games), a symptom indistinguishable from network failure. The relaunch flow
  is native: `RelaunchElevatedShare`/`ParseElevatedShareArgs` in `ElevatedShare.cpp`, driven by
  the Win32 UI (`client/windows/win32/MainMenuWindow.cpp` + `main.cpp`) — when starting a share
  that needs control or a missing firewall rule, the app relaunches `Deskhub.exe` with the
  `runas` verb, passing the share request on the command line so the elevated instance resumes
  without re-picking sources. `IsProcessElevated()` backs the "input will
  NOT reach apps running as administrator" warning in `RunAgent`.
- **`net/Firewall.h`.** The host binds `INADDR_ANY` and Windows Firewall drops unsolicited inbound
  UDP by default — the classic "LAN connect just times out". `EnsureHostFirewallRule()` adds a
  program-scoped allow rule via `INetFwPolicy2` covering all three profiles (port-independent, so
  the user can change ports). Adding requires admin (hence the UAC flow above); failure is a
  warning, not fatal. `HostFirewallRulePresent()` is the read-only probe the Win32 UI
  uses to decide whether elevation is needed.
- **Pre-session beacon.** `RunAgent` owns a `deskhub::Beacon`
  (`core/include/deskhub/discovery/Beacon.h`) answering the two pre-session queries:
  `LIST_SOURCES → SOURCE_LIST` and probe `PING (sid=0) → PONG`. (The `DISCOVER → ANNOUNCE`
  branch was removed on 2026-07-27 together with all LAN discovery.) The beacon only *builds*
  reply bytes; the Recv loop sends them back to the datagram's origin — deliberately before
  `replyAddr` is updated, so a stranger probing the network can never hijack the session's
  send path. `publishRows` refreshes the beacon's source list each second.

## 8. Clipboard sync — removed

Two-way clipboard sync (GĐ8/9) was removed 2026-07-27: no `ClipboardSync` classes,
no `CLIPBOARD` message, no `shareClipboard` option. (The "Copy address" buttons that
write to the LOCAL clipboard are unrelated and remain.)

## 9. macOS agent specifics

The macOS agent (`client/macos/app/cpp/`) is a deliberate port of the Windows loop — same
pipeline-per-source structure, same routing, same cached-frame/keepalive/resize/pause logic — with
these differences:

- Non-blocking `AgentLoop` class API for SwiftUI (§1); status is polled, not pushed.
- **Permissions** (`agent/Permissions.h`): missing permissions fail *silently* on macOS — no
  Screen Recording means `SCShareableContent` only returns the app's own windows; no Accessibility
  means `CGEventPost` "succeeds" without delivering anything. `Start` therefore refuses to run
  without Screen Recording, and `InputInjector::Init` checks `HasAccessibility` and logs plainly.
  Accessibility is required for every share (input is always on). App-level flow: `14-macos-app.md`.
- The frame cache is a retained `CVPixelBufferRef` (SCStream's queue depth is raised to tolerate
  it) instead of a texture copy.
- **No `Beacon`:** the macOS Recv loop answers `LIST_SOURCES` inline (`BuildSourceList`) but does
  not answer `DISCOVER` or probe `PING`, so a macOS host does not appear in network scans and must
  be reached by typed address.
- One shared `LocalInputMonitor` is passed to every injector via `SetLocalMonitor` (Windows uses a
  process-global timestamp instead).

Transport-per-platform notes: `11-platform-transport.md`. Diagnostics counters emitted by both
loops (encode ms, IDR size/burst, send failures, Recv-loop stalls): `09-diagnostics.md`.
