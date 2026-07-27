# 03 — Client Role

The Client (viewer) role connects to a machine running the Agent role (02-agent.md),
receives its H.264 video stream over UDP, renders it with the platform's hardware
decoder, and forwards keyboard/mouse/touch input back. Desktop apps (Windows, macOS)
contain both roles in one binary; Android and iOS are client-only.

All protocol logic lives in `core/` (portable C++20, no sockets, no clocks — time and
bytes are injected). Each platform contributes a thin loop that wires four pieces
together: a UDP socket, the `deskhub::ClientSession` state machine, the
`deskhub::Reassembler`, and a hardware decoder.

| Platform | Client loop | UI |
|----------|-------------|-----|
| Windows | `client/windows/cpp/ClientApi.cpp` (`DhClientHandle::Run`) | Plain Win32, `client/windows/win32/` (`Viewer.cpp`) |
| macOS | `client/macos/app/cpp/ClientLoop.cpp` | SwiftUI, `client/macos/app/swift/` |
| Android | `client/android/app/src/main/cpp/ClientLoop.cpp` | Jetpack Compose, `client/android/app/src/main/java/com/deskhub/app/` |
| iOS | `client/ios/app/cpp/ClientLoop.cpp` | SwiftUI, `client/ios/app/swift/` |
| Web | designed only — see 10-web-client.md; no code under `client/` yet | — |

## 1. Connect flow

Four steps, each with its own mechanism. Steps 1–2 run *before* any session exists,
on their own short-lived sockets.

### 1a. No LAN discovery

LAN broadcast discovery (DISCOVER/ANNOUNCE, `HostRegistry`, `dh_discover_scan`) was
removed 2026-07-27: every client connects by a typed `ip:port` (recents make retyping
rare). Broadcast could not cross routers anyway, so Tailscale addresses were always
typed by hand.

### 1b. Source listing (LIST_SOURCES / SOURCE_LIST)

`QuerySources()` — one copy per platform (`client/*/…/net/SourceQuery.cpp`, same
shape everywhere) — sends `LIST_SOURCES` to the host and waits for `SOURCE_LIST`:
the set of displays the host is sharing (whole monitors only — window sources were
removed 2026-07-27), each with a `sourceId`, name and
size. It is a blocking one-shot exchange (retries for up to ~3 s, must be called off
the UI thread; Kotlin wraps it in a `suspend fun`, Swift in `Task.detached`, the
Win32 app in a worker thread). A silent host is **not** an error: the caller assumes "old host /
single source" and connects to source 0.

### 1c. No password auth

The password/auth layer (GĐ10: PBKDF2 challenge–response, device tokens) was
**removed on 2026-07-27**: the app targets trusted LANs (or Tailscale, which is its
own trust boundary), so the first HELLO goes straight to `HELLO_ACK`. Neither the
handshake nor the stream is encrypted — do not expose the host port to untrusted
networks.

### 1d. Session start (ClientSession)

`deskhub::ClientSession` (`core/include/deskhub/session/ClientSession.h`) is the
per-session state machine, owner of everything on the Control channel:

```
Idle ─Start()→ Hello ─HELLO_ACK→ Starting ─first video packet→ Streaming
                 │ 10 s silence      └── BYE / 5 s timeout ────────┤
                 └────────────────────────────────────────────→ Dead
```

HELLO and START are resent every 0.5 s because UDP gives them no ACK; the first
video packet is the only proof that START arrived. `onReady` delivers the
`NegotiatedParams` (codec, size, fps, host timebase)
so the caller can build its decoder; `onReconfig` fires when the
host changes resolution mid-session. `Dead` is terminal — reconnecting means a new
session.

## 2. Main loop: receive → reassemble → decode → render

All four platforms use the same two-native-thread shape (plus the UI thread):

- **Net thread** — `recvfrom` loop. Video-channel packets go *straight* into
  `Reassembler::Push`/`PushFec` (hot path, deliberately not routed through the
  session), with `ClientSession::NotifyVideoPacket` called for timeout/state
  purposes; everything else goes to `ClientSession::HandlePacket`. Completed frames
  pop via `PopReady` and are queued for decode. The same thread drains the input
  queue and calls `session.Tick(now)`.
- **Decode thread** — pops frames from a bounded queue (`kMaxQueuedFrames = 3`,
  drop-oldest, never block the producer) and feeds the platform decoder.

The split is load-bearing: if decoding ran on the net thread, `recvfrom` would stall
while the decoder is busy, the OS UDP buffer would overflow, and packets would be
lost *before the program ever sees them* — loss that neither FEC nor keyframe
requests can repair (rationale in every `ClientLoop.h`).

On Android/macOS/iOS the render target (Surface / `AVSampleBufferDisplayLayer`) is
owned by the main thread, so handover uses a generation-counted handshake
(`SetWindow`/`SetLayer` block until the decode thread acknowledges releasing the old
target — destroying an `ANativeWindow` while the codec renders into it is a
use-after-free). On Windows there is no handover: `dh_client_start_hwnd` creates the
device and for-HWND swapchain on the caller's (UI) thread before the loop starts.

### Loss handling, feedback, and clock sync (all on the net thread)

- **NACK / retransmit:** each pass, `Reassembler::PlanNack(now, minRtt, …)` yields
  up to 64 missing fragment indices; `ClientSession::SendNack` ships them so the
  host retransmits from its `RetransmitCache`. See 06-transport.md.
- **Keyframe requests:** on an unrecoverable loss event, when waiting for an IDR,
  on decoder failure, or on frame-queue overflow, `RequestKeyframe()` sets a flag
  that `Tick` re-sends every 250 ms until an IDR arrives (`CancelKeyframeRequest`).
- **Stats feedback:** `deskhub::LinkStats`
  (`core/include/deskhub/control/LinkStats.h`) turns the Reassembler's cumulative
  counters into one-second windows (fps, kbps, loss %, loss-run histogram).
  Once per second the client sends `MakeFeedback(window, rtt)` via `SendFeedback` —
  even at zero loss, because the host's `BitrateController` treats silence as a
  dead link and will not raise the bitrate without a "path is clean" signal.
- **RTT / e2e estimate:** `Tick` sends PING every second; each PONG updates
  `lastRttUs`. End-to-end latency is estimated inline in each loop from atomics:
  `ackDelta` (local clock − host `timebaseUs` at HELLO_ACK) minus `minRTT/2` gives
  the clock offset, and `e2e = now − offset − frame pts`. (The unused
  `ClockSync`/`LatencyTrace` classes in core that duplicated this were removed
  2026-07-27 — the inline estimate is the only implementation.)

## 3. Decode / render backends

| Platform | Decoder | Render path |
|----------|---------|-------------|
| Windows | `MfDecoder` (`client/windows/cpp/decode/MfDecoder.h`) — Media Foundation sync MFT with D3D11VA, `MF_LOW_LATENCY`; only implementation of `IVideoDecoder` | NV12 texture-array slice → `PanelRenderer` (D3D11 Video Processor NV12→BGRA) into a for-HWND swapchain on a child window of the Win32 app (`dh_client_start_hwnd` — the only render path since the WinUI3/composition path was removed 2026-07-27) |
| macOS | `VtDecoder` (`client/macos/app/cpp/decode/VtDecoder.mm`) — VideoToolbox via `AVSampleBufferDisplayLayer`; converts Annex-B → AVCC and builds `CMVideoFormatDescription` from in-band SPS/PPS | the layer decodes **and** presents through the compositor; enqueueing a `CMSampleBuffer` is the entire render step |
| iOS | `VtDecoder` (`client/ios/app/cpp/decode/VtDecoder.mm`) — the macOS file is a copy of this one | same `AVSampleBufferDisplayLayer` path, layer hosted by `VideoLayerView.swift` |
| Android | `MediaCodecDecoder` (`client/android/app/src/main/cpp/decode/MediaCodecDecoder.h`) — NDK `AMediaCodec` configured directly with the `ANativeWindow` | `AMediaCodec_releaseOutputBuffer(..., true)` *is* the render; frames never touch the CPU |
| Web | WebCodecs `VideoDecoder` + WebTransport + WASM core — **design only**, see 10-web-client.md | canvas/WebGL (designed) |

On Windows, decoded frames are only valid inside the decoder callback and must be
identified by `texture` **and** `subresource` (the decoder pools frames as
texture-array slices — `decode/IVideoDecoder.h`).

## 4. Input capture and forwarding

All platforms funnel events into `ClientSession::QueueInput`; the core
`InputSender` assigns sequence numbers, batches events, and appends a redundancy
tail (last 8 events per datagram, re-sent twice at 25 ms intervals) so each event
crosses the wire ~3 times in ~50 ms — losing a key-*up* packet must never leave a
key stuck. Details in 07-input.md.

- **Desktop (Windows viewer, macOS):** real keyboard and mouse. Key events carry
  Windows VK **and** scancode (macOS translates `NSEvent.keyCode` via
  `client/macos/app/cpp/input/MacKeyMap.cpp`); mouse is absolute (0..65535
  normalized to the video rect) with a relative-delta mode for pointer lock / FPS
  games (F9, `dh_client_mouse_move_rel` / `QueueMouseMoveRel`); wheel in
  `WHEEL_DELTA` units. macOS additionally calls `ReleaseAllInput` on focus loss so
  held keys are released immediately instead of after the host's timeout.
- **Mobile (Android, iOS):** a virtual **trackpad** overlay (`TrackpadOverlay` in
  `StreamActivity.kt`, `TouchInputView.swift`) — persistent cursor, finger drags
  move it (works over letterbox too), tap = left click at cursor, double-tap =
  right click, long-press-drag = held left drag. Characters from the soft keyboard
  go through `QueueCharTap` (core `KeyMap`, US layout → Shift sequences); a
  shortcut bar sends discrete key taps, with key-up delayed 50 ms (`kTapHoldUs`) so
  frame-polling games see the press. Every sender still funnels through
  `NativeClient`, but there is no gate in it any more — the view-only toggle was
  removed 2026-07-27.

`SET_FOCUS` is still sent event-driven (3 repeats, 50 ms apart) when the preview
gains or loses focus; the host now acts only on `false`, releasing any held keys for
the session (the raise-to-foreground behavior on `true` was removed 2026-07-27 with
window sharing). Clients no longer ask whether the host accepts input: it always
does, and the `inputAccepted` flag was removed from HELLO_ACK on 2026-07-27.

## 5. Clipboard — removed

Two-way clipboard sync (GĐ8) was removed 2026-07-27 along with `ClipboardAssembler`
and the desktop pasteboard wiring.

## 6. Shared UX features

- **Recents** — persisted list of previously connected machines, deduplicated by
  address, with a link-type guess for display: `ui/Recents.kt` (SharedPreferences,
  max 12), `Recents.swift` on iOS and macOS (UserDefaults). (The Windows `Recents.cs`
  went with the WinUI3 app, removed 2026-07-27; the Win32 app has no recents list.)
  Tapping a recent pre-fills the connect screen rather than connecting directly.
- **Stats overlay** — every loop produces a 1 Hz `StatusLine()`
  (`fps · Mbps · loss % · RTT · e2e`) that the UI polls; Android additionally
  parses RTT out of it for a signal indicator in `StreamActivity.kt`.

## 7. How each platform binds to core

| Platform | Binding | Doc |
|----------|---------|-----|
| Windows | Flat C API (`client/windows/cpp/DeskhubApi.h`, `dh_client_*`) — statically linked into the Win32 app's exe (the `deskhub_native.dll` export surface for the WinUI3 app was removed 2026-07-27) | 01-architecture.md |
| Android | JNI: `NativeClient.kt` (single Kotlin `object`, name-mangled to `JniBridge.cpp`); one global `ClientLoop`, start/stop guarded by generation tokens against overlapping Activity lifecycles | 08-android-client.md |
| iOS | Flat C facade `client/ios/app/cpp/DeskhubClient.h`/`.mm` (ObjC++), imported through the Swift bridging header; one global session | 12-ios-client.md |
| macOS | ObjC++ bridge `client/macos/app/cpp/DeskhubBridge.mm` wrapping `ClientLoop` for SwiftUI | 14-macos-app.md |

Wire formats for every message named above are specified in 04-protocol.md;
reassembly, FEC and NACK mechanics in 06-transport.md.
