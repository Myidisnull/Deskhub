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
| Windows | `client/windows/cpp/ClientApi.cpp` (`DhClientHandle::Run`) | WinUI3 (C#), `client/windows/csharp/` |
| macOS | `client/macos/app/cpp/client/ClientLoop.cpp` | SwiftUI, `client/macos/app/swift/` |
| Android | `client/android/app/src/main/cpp/ClientLoop.cpp` | Jetpack Compose, `client/android/app/src/main/java/com/deskhub/app/` |
| iOS | `client/ios/app/cpp/ClientLoop.cpp` | SwiftUI, `client/ios/app/swift/` |
| Web | designed only — see 10-web-client.md; no code under `client/` yet | — |

## 1. Connect flow

Four steps, each with its own mechanism. Steps 1–2 run *before* any session exists,
on their own short-lived sockets.

### 1a. LAN discovery (DISCOVER / ANNOUNCE)

The client broadcasts `DISCOVER` on every local network interface and collects
`ANNOUNCE` replies from agents running `deskhub::Beacon`. The platform half
(`client/windows/cpp/net/Discovery.cpp`, `ScanForHosts`) opens the socket and blocks
for a ~1.2 s window; the logic half is `deskhub::HostRegistry`
(`core/include/deskhub/discovery/HostRegistry.h`), which:

- merges replies **by `hostId`, not by address** (a machine on Wi-Fi + Ethernet
  answers twice; the lower-RTT address wins),
- keeps a stable sort order (name, then hostId) so the list doesn't reshuffle under
  the user's finger,
- expires hosts silent for `kHostStaleUs` (6 s), and discards late replies from a
  previous scan via a per-scan `probeId` (`BeginScan`/`OnAnnounce`).

Broadcast does not cross routers, so hosts reached over routed links (e.g. Tailscale
/32) must be typed manually — a design limit, stated in `Discovery.h`.

**Status per platform:** implemented only on Windows (`dh_discover_scan` in
`client/windows/cpp/DeskhubApi.h`). The macOS UI deliberately omits the "found on
your network" section until discovery is ported (see the header comment in
`client/macos/app/swift/HomeView.swift`); Android and iOS rely on typed addresses
and recents.

### 1b. Source listing (LIST_SOURCES / SOURCE_LIST)

`QuerySources()` — one copy per platform (`client/*/…/net/SourceQuery.cpp`, same
shape everywhere) — sends `LIST_SOURCES` to the host and waits for `SOURCE_LIST`:
the set of displays/windows the host is sharing, each with a `sourceId`, name and
size. It is a blocking one-shot exchange (retries for up to ~3 s, must be called off
the UI thread; Kotlin wraps it in a `suspend fun`, Swift in `Task.detached`, C# in
`Task.Run`). A silent host is **not** an error: the caller assumes "old host /
single source" and connects to source 0.

### 1c. Password auth (challenge–response)

Optional; the host decides. Primitives are in
`core/include/deskhub/auth/PasswordAuth.h`, the client-side driver is inside
`ClientSession`:

```
HELLO (+deviceToken if remembered)       →  host
AUTH_CHALLENGE  salt(16) iters nonce(32) ←  host        (skipped if token matched)
key   = PBKDF2(password, salt, iters)       — kAuthKdfIterations = 100 000
proof = HMAC-SHA256(key, nonce ‖ clientId)
AUTH_RESPONSE  proof(32)                 →  host
HELLO_ACK (sessionId, + new deviceToken) ←  host
```

The password never crosses the wire; the nonce prevents replay, and mixing
`clientId` into the proof binds it to one client. `ClientSession::SetPassword`
caches the derived key across handshake retries (PBKDF2 costs ~100 ms while HELLO is
resent every 0.5 s) and re-arms the 10 s give-up timer so a slow typist doesn't kill
the session. `onPasswordNeeded` tells the UI to prompt; `onDeviceToken` delivers a
remember-this-device token (sent over the wire exactly once). Note the scope,
spelled out in `PasswordAuth.h`: this gates session *setup* only — the stream itself
is not encrypted.

**Status per platform:** fully wired on Android (`Phase::NeedPassword`,
`NativeClient.nativeSubmitPassword`, storage in `ui/Credentials.kt`) and iOS
(`DHPhaseNeedPassword`, `Credentials.swift`). The Windows viewer (`ClientApi.cpp`)
and the macOS `ClientLoop` do not yet call `SetPassword` — they can only connect to
hosts without a password.

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
`NegotiatedParams` (codec, size, fps, host timebase, `inputAccepted`,
`clipboardEnabled`) so the caller can build its decoder; `onReconfig` fires when the
host changes resolution mid-session. `Dead` is terminal — reconnecting means a new
session.

## 2. Main loop: receive → reassemble → decode → render

All four platforms use the same two-native-thread shape (plus the UI thread):

- **Net thread** — `recvfrom` loop. Video-channel packets go *straight* into
  `Reassembler::Push`/`PushFec` (hot path, deliberately not routed through the
  session), with `ClientSession::NotifyVideoPacket` called for timeout/state
  purposes; everything else goes to `ClientSession::HandlePacket`. Completed frames
  pop via `PopReady` and are queued for decode. The same thread drains the input
  queue, handles clipboard, and calls `session.Tick(now)`.
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
use-after-free). On Windows there is no handover: `dh_client_start` creates the
composition swapchain on the caller's (UI) thread and C# attaches it once.

### Loss handling, feedback, and clock sync (all on the net thread)

- **NACK / retransmit:** each pass, `Reassembler::PlanNack(now, minRtt, …)` yields
  up to 64 missing fragment indices; `ClientSession::SendNack` ships them so the
  host retransmits from its `RetransmitCache`. See 06-transport.md.
- **Keyframe requests:** on an unrecoverable loss event, when waiting for an IDR,
  on decoder failure, or on frame-queue overflow, `RequestKeyframe()` sets a flag
  that `Tick` re-sends every 250 ms until an IDR arrives (`CancelKeyframeRequest`).
- **Stats feedback:** `deskhub::LinkStats`
  (`core/include/deskhub/control/LinkStats.h`) turns the Reassembler's cumulative
  counters into one-second windows (fps, kbps, loss %, loss-run histogram, e2e).
  Once per second the client sends `MakeFeedback(window, rtt)` via `SendFeedback` —
  even at zero loss, because the host's `BitrateController` treats silence as a
  dead link and will not raise the bitrate without a "path is clean" signal.
- **RTT / e2e estimate:** `Tick` sends PING every second; each PONG updates
  `lastRttUs`. End-to-end latency is estimated inline in each loop from atomics:
  `ackDelta` (local clock − host `timebaseUs` at HELLO_ACK) minus `minRTT/2` gives
  the clock offset, and `e2e = now − offset − frame pts`. The dedicated
  `deskhub::ClockSync` (min-filter with 10 s refresh) and `deskhub::LatencyTrace`
  (60-sample, 320 ms chart buffer) classes exist in `core/include/deskhub/control/`
  with tests, but are **not yet wired into any client loop** — the inline estimate
  is what ships today.

## 3. Decode / render backends

| Platform | Decoder | Render path |
|----------|---------|-------------|
| Windows | `MfDecoder` (`client/windows/cpp/decode/MfDecoder.h`) — Media Foundation sync MFT with D3D11VA, `MF_LOW_LATENCY`; only implementation of `IVideoDecoder` | NV12 texture-array slice → `PanelRenderer` (D3D11 Video Processor NV12→BGRA) into a composition swapchain attached to a WinUI3 `SwapChainPanel` |
| macOS | `VtDecoder` (`client/macos/app/cpp/client/VtDecoder.mm`) — VideoToolbox via `AVSampleBufferDisplayLayer`; converts Annex-B → AVCC and builds `CMVideoFormatDescription` from in-band SPS/PPS | the layer decodes **and** presents through the compositor; enqueueing a `CMSampleBuffer` is the entire render step |
| iOS | `VtDecoder` (`client/ios/app/cpp/VtDecoder.mm`) — the macOS file is a copy of this one | same `AVSampleBufferDisplayLayer` path, layer hosted by `VideoLayerView.swift` |
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
  frame-polling games see the press. A view-only toggle gates all senders in one
  place (`NativeClient.viewOnly`).

`SET_FOCUS` is sent event-driven (3 repeats, 50 ms apart) so the host brings the
viewed window to the foreground before injecting. The `inputAccepted` flag from
HELLO_ACK tells the UI to hide input affordances for view-only hosts.

## 5. Clipboard (client side)

`ClientSession` carries both directions: `onClipboard` delivers host-copied text
(fragments reassembled by `ClipboardAssembler`), `SendClipboard` ships local copies,
gated on the host's `clipboardEnabled` flag from HELLO_ACK.

Wired today on the two desktop viewers only: Windows (`ClipboardSync` in
`ClientApi.cpp`, bidirectional with content-based echo suppression) and macOS
(`ClientLoop::SetLocalClipboard` / `TakeRemoteClipboard` bridged to `NSPasteboard`).
The Android and iOS client loops have no clipboard path yet.

## 6. Shared UX features

- **Recents** — persisted list of previously connected machines, deduplicated by
  address, with a link-type guess for display: `ui/Recents.kt` (SharedPreferences,
  max 12), `Recents.swift` on iOS and macOS (UserDefaults), `Recents.cs` on Windows.
  Tapping a recent pre-fills the connect screen rather than connecting directly.
- **Stored credentials** (Android/iOS) — `ui/Credentials.kt` (Android Keystore
  AES-GCM) and `Credentials.swift` (Keychain, `WhenUnlockedThisDeviceOnly`) persist
  three things: a *stable* `clientId` (the host keys its trusted-device list on it),
  an optional per-host password, and the per-host `deviceToken`. The token is
  read-once from the native layer (`TakeDeviceToken`) and must be stored
  immediately.
- **Stats overlay** — every loop produces a 1 Hz `StatusLine()`
  (`fps · Mbps · loss % · RTT · e2e`) that the UI polls; Android additionally
  parses RTT out of it for a signal indicator in `StreamActivity.kt`.

## 7. How each platform binds to core

| Platform | Binding | Doc |
|----------|---------|-----|
| Windows | Direct C++; a flat C API (`client/windows/cpp/DeskhubApi.h`, `dh_client_*`) exported from `deskhub_native.dll`, P/Invoked by the WinUI3 app | 01-architecture.md |
| Android | JNI: `NativeClient.kt` (single Kotlin `object`, name-mangled to `JniBridge.cpp`); one global `ClientLoop`, start/stop guarded by generation tokens against overlapping Activity lifecycles | 08-android-client.md |
| iOS | Flat C facade `client/ios/app/cpp/DeskhubClient.h`/`.mm` (ObjC++), imported through the Swift bridging header; one global session | 12-ios-client.md |
| macOS | ObjC++ bridge `client/macos/app/cpp/DeskhubBridge.mm` wrapping `ClientLoop` for SwiftUI | 14-macos-app.md |

Wire formats for every message named above are specified in 04-protocol.md;
reassembly, FEC and NACK mechanics in 06-transport.md.
