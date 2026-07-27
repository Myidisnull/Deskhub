# 14 — macOS App

**One app, both roles.** Like the Windows reference build (01-architecture.md §1), the
macOS app is a single SwiftUI application that can act as **Agent/host** (share one or
more displays of this Mac — per-window sharing was removed 2026-07-27) and as
**Client** (view and control another machine) —
even both at the same time, since the two roles are independent objects behind the
bridge. This is the fundamental difference from iOS/Android, which are client-only
(11-platform-transport.md).

**Status: implemented and tested, both roles** — remaining known gaps in §8.

## 1. Architecture and layering

```
SwiftUI views (App/ContentView/HomeView/ShareView/ConnectView/SourcePickerView/StreamView/RemoteView)
        │  read models, call actions
Swift models: SessionModel (client role), AgentModel (host role)   — @MainActor @Observable
        │  the ONLY callers of the C facade
Swift wrappers: DeskhubClient.swift (dh_*), DeskhubAgent.swift (dha_* + permissions)
        │  via Deskhub-Bridging-Header.h
C facade: client/macos/app/cpp/DeskhubBridge.h / .mm
        │  two globals: g_client (ClientLoop) + g_agent (AgentLoop), one mutex each
C++/ObjC++ role stacks: cpp/client/*, cpp/agent/*, cpp/net/*, cpp/input/*
        │  reuse core/ (deskhub::ClientSession, HostSession, Packetizer, Reassembler,
        │  BitrateController, RetransmitCache, Wire.h) unchanged
```

- `client/macos/app/swift/App.swift` — entry point; one `WindowGroup` (default
  1280×840, min 1040×680), no custom title bar.
- `client/macos/app/swift/ContentView.swift` — navigation via a `Route` enum:
  `home → connect → sourcePicker → stream` (client branch) and `home → share` (host
  branch). A left `SideRail` (home / connect / share + theme and EN‑VI language
  toggles, backed by `AppState`) is hidden on the `stream` route. `SessionModel` and
  `AgentModel` are owned here so sessions survive navigation.
- `DeskhubBridge.h` documents which facade calls **block**: `dh_list_sources` (~3 s),
  `dha_list_share_sources` (~2 s), `dha_start` (up to ~10 s). Swift calls these via
  `Task.detached`; everything else is safe on the main thread. `dh_set_layer`
  deliberately drops the client mutex before waiting for the decode thread's ack,
  otherwise main-thread status polls would deadlock (comment in `DeskhubBridge.mm`).
- Prefixes: `dh_*` = client role + shared utilities (key map, permissions),
  `dha_*` = agent role.
- Logging (`cpp/Log.h`) is `fprintf(stderr)` — visible in the Xcode console; user-facing
  errors must instead travel through `StatusLine()`/`EndReason()`.

**Threading model.** UI polls state on 500 ms `Timer`s in both models. Client role:
three threads — Main (layer handover, polling, input queueing), Net (`recvfrom` with
10 ms timeout → `ClientSession`/`Reassembler`), Decode (frame queue → `VtDecoder` →
layer); see `cpp/client/ClientLoop.h`. Agent role: per source one SCStream capture
queue, one VideoToolbox internal callback thread (serialized by
`VtEncoder::emitMutex_`), plus a single shared Recv thread (`recvfrom` with 100 ms
timeout) that routes packets, ticks all sessions and publishes stats; see the thread
map at the top of `cpp/agent/AgentLoop.cpp`.

## 2. Agent role (share this Mac)

`AgentLoop` (`cpp/agent/AgentLoop.h/.cpp`) is the host orchestrator. Unlike the
Windows `RunAgent()`, `AgentLoop::Start()` does not block for the session lifetime:
it opens the socket, builds one `SourcePipeline` per source, waits for each source's
first frame (up to 10 s, to learn the size offered in HELLO_ACK), then spawns the
Recv thread and returns. Notable behavior, all as coded:

- **One UDP socket for all sources** (GĐ6). If the requested port is busy it retries
  upward (up to 64 ports); `dha_port()` reports the port actually bound. Packet
  routing: `LIST_SOURCES` answered with all live sources; `HELLO` routed by
  `sourceId`; everything else by `sessionId`. Source ids are never reused.
- **Live add/remove.** `AddSource`/`RemoveSource` only post to a command mailbox; the
  Recv thread executes them on its next loop (this is why ShareView's checkboxes work
  mid-session without dropping the viewer).
- **IDR on demand + last-frame cache.** SCStream only delivers frames when content
  changes, so the pipeline retains (not copies) the last `CVPixelBufferRef`; a
  pending IDR request on a static source re-encodes the cached frame (after 200 ms of
  silence), and a ~2 fps keepalive re-encode keeps the client's presentation clock
  running. `forceIdr` is an atomic flag consumed on the capture path.
- **Resize handling.** Size changes (display mode switches) rebuild the encoder and
  send `RECONFIG` + IDR; sources smaller than 160×64 (`kMinEncodeW/H`) are *paused*,
  not failed, and resume when the size comes back. A vanished display shuts down only
  that pipeline; the session keeps running.
- **Congestion control & recovery.** Per-source `deskhub::BitrateController` acts on
  `FEEDBACK` (bitrate changes via `VtEncoder::SetBitrate`, FEC toggle), and a
  `RetransmitCache` answers NACKs (GĐ7) instead of forcing an IDR.

Component specifics:

- **`agent/SourceEnum.mm` — enumeration.** Uses `SCShareableContent`, so the list
  matches exactly what ScreenCaptureKit can capture, and lists its `SCDisplay`s only
  (pixel sizes from `SCDisplay`). The window walk — "App — Title" labels,
  points→pixels conversion, self/untitled/tiny filtering — was removed 2026-07-27
  with window sharing. Synchronous wrapper over the
  async API with a 2 s semaphore timeout — must be called off the main thread.
- **`agent/ScreenCapture.mm` — capture.** One `SCStream` per source. Configuration:
  pixel format `420v` (NV12 video-range, fed straight to VideoToolbox with no color
  conversion), `minimumFrameInterval = 1/fps` (a *cap* — frames arrive only on
  change), `showsCursor = YES`, `queueDepth = 5` (AgentLoop retains one buffer as
  cache, VideoToolbox holds another), `scalesToFit = NO`, no audio. Only
  `SCFrameStatusComplete` frames are forwarded; timestamps are the project clock
  (`NowUs()`), not the sample buffer PTS. Frames are delivered on a per-source serial
  `USER_INTERACTIVE` queue. A 500 ms dispatch timer compares the real display size
  (`CGDisplayPixelsWide`×scale) with the buffer size
  and calls `updateConfiguration` on mismatch — SCStream does not resize itself — and
  the same timer detects a vanished display (`Closed()`). Sizes are rounded
  down to even numbers (H.264 chroma requirement).
- **`agent/VtEncoder.mm` — encoding.** H.264 via `VTCompressionSession` (hardware
  requested but not required; `BackendName()` reports which was used). Low-latency
  knobs: `RealTime = true`, `AllowFrameReordering = false` (no B-frames), profile
  High/AutoLevel, CABAC, **infinite GOP** (`MaxKeyFrameInterval` *and*
  `MaxKeyFrameIntervalDuration` = INT32_MAX) with IDR only on demand,
  `AverageBitRate` plus `DataRateLimits` (1.5× bitrate per 1 s window) as a burst
  cap. `SetBitrate` retunes mid-session without rebuilding. Output arrives async on
  VideoToolbox's own thread; `OnEncoded` converts AVCC length prefixes to Annex-B
  start codes and prepends SPS/PPS to every IDR (the protocol requires in-band
  parameter sets), serialized under `emitMutex_` so the single-threaded
  `deskhub::Packetizer` stays safe.
- **`agent/InputInjector.mm` — injection.** Builds CGEvents from an event source
  created with `kCGEventSourceStateHIDSystemState` and posts to `kCGHIDEventTap`.
  Keys: protocol VK codes are translated to Carbon keycodes by
  `cpp/input/MacKeyMap.cpp` (the single key table, shared by both roles; VK is
  preferred over scancode since macOS has no scancode concept); modifier flags are
  rebuilt from the injector's own held-modifier ledger and attached to every event.
  Absolute mouse coords (0..65535) map into the shared display's bounds *in points*
  (`Init(displayId)`); relative moves carry raw deltas in `kCGMouseEventDeltaX/Y` and clamp to the
  union of all screens. Click counting (`kCGMouseEventClickState`, 500 ms / 4 pt) is
  synthesized so double-clicks work; wheel deltas convert 120 → 3 lines. Two safety
  gates in `Apply()`: enabled flag and **host wins** (below) — the third, the
  foreground gate (`TargetHasFocus`/`FocusTarget`), was removed 2026-07-27 with
  window sharing. `ReleaseAll()` un-sticks held keys on
  disconnect and on `SET_FOCUS(false)`. Every injected event is stamped with
  `LocalInputMonitor::kUserData` in `kCGEventSourceUserData`.
- **`agent/LocalInputMonitor.h/.mm` — "host wins".** An NSEvent global monitor (not a
  CGEventTap) records the last *physical* mouse/keyboard activity; injected events
  are recognized by the `kUserData` stamp and ignored. While the local user is active
  (`kQuietUs` = 1 s), remote input is suppressed and held keys are released.

## 3. Permissions (`agent/Permissions.h/.mm`)

Exactly two system permissions, both host-role only, both **silent failures** on
macOS:

- **Screen Recording** — checked with `CGPreflightScreenCaptureAccess()`, requested
  with `CGRequestScreenCaptureAccess()`. Without it, `SCShareableContent` yields no
  shareable displays, so the source list is
  simply empty with no error. The request dialog appears **once per app install**;
  after granting, macOS requires an app restart. Required for any sharing.
- **Accessibility** — checked with `AXIsProcessTrusted()`, requested with
  `AXIsProcessTrustedWithOptions(kAXTrustedCheckOptionPrompt)`. Without it,
  `CGEventPost` "succeeds" but no event reaches any app. Takes effect immediately, no
  restart. Needed only when *Allow input* is on; view-only sharing works without it.

`OpenScreenRecordingSettings`/`OpenAccessibilitySettings` open the exact
`x-apple.systempreferences:…Privacy_ScreenCapture` / `…Privacy_Accessibility` panes.
UI behavior as coded: `AgentModel.refreshPermissions()` re-reads both flags on every
entry to Home/Share (no restart needed to *detect* a change); `ShareView` shows a
banner and **disables the Share button** while Screen Recording is missing, and shows
an Accessibility banner only when input is allowed but not granted.
`AgentLoop::Start` and `InputInjector::Init` also log warnings but do not hard-fail —
the permission may have just been granted while preflight still caches the old value.

## 4. Client role (view another machine)

- **`cpp/client/ClientLoop.h/.cpp`** — a close port of the iOS ClientLoop with a
  desktop-grade input channel (separate key down/up with VK+scancode, mouse wheel,
  true relative mouse, `ReleaseAllInput`). Net thread: video-channel
  packets bypass `ClientSession` straight into the `Reassembler` (session only gets
  `NotifyVideoPacket`); assembled frames go into a 3-deep queue toward the Decode
  thread, dropping the *oldest* on overflow; all keyframe-request reasons (loss,
  waiting-for-IDR, decoder failure, queue overflow) funnel through one place. HELLO
  advertises H.264 only, max 3840×2160, 60 fps. Once per second it closes a
  `LinkStats` window, updates the UI status line (fps / Mbps / loss / RTT / e2e) and
  sends `FEEDBACK`. Layer handover uses a generation-counted handshake so `SetLayer`
  blocks until Decode confirms it released the old `AVSampleBufferDisplayLayer`.
- **`cpp/client/VtDecoder.h/.mm`** — copied from iOS. Converts Annex-B → AVCC, builds
  a `CMVideoFormatDescription` from in-band SPS/PPS, and enqueues `CMSampleBuffer`s
  directly into the `AVSampleBufferDisplayLayer` — the layer decodes in hardware and
  composites; there is no separate renderer. Known caveat (documented in the header):
  the e2e timestamp is taken at *enqueue*, not at actual display.
- **Render + input surface.** `swift/RemoteView.swift` is one `NSView`
  (`RemoteVideoView`) whose **backing layer is the display layer**
  (`makeBackingLayer`), so decoded frames go straight to the compositor. It is
  `isFlipped` (top-left origin, matching the protocol), accepts first mouse, and
  captures the full keyboard (`keyDown`/`keyUp` skipping auto-repeats,
  `flagsChanged` diffing for modifiers, translated via `dh_map_key`). Two mouse
  modes: absolute (normalized 0..65535 inside the letterboxed `videoRect`; points on
  the black bars send nothing) and **relative/mouse-lock via F9**
  (`CGAssociateMouseAndMouseCursorPosition(false)` + hidden cursor, raw deltas for FPS
  games; F9 is handled locally and never forwarded). Trackpad scrolling converts
  precise deltas to at least one 120-unit notch. Losing first-responder releases all
  input and unlocks the mouse.
- **`swift/StreamView.swift`** — the viewer screen: video full-window, forced dark
  scheme, host label + state pill (top-left), stats HUD with RTT sparkline
  (top-right), control HUD (mouse lock, aspect-fit/fill toggle, state chip, End)
  bottom-center; connecting and "session ended" overlays (reason from
  `dh_end_reason`). `onDisappear` revokes the layer and disconnects.
- **`swift/SessionModel.swift`** — the single choke point for input: everything is
  gated on `viewOnly || !hostAcceptsInput` (the latter polled from the HELLO_ACK
  `inputAccepted` flag, GĐ9). Its 500 ms poll parses `"RTT n ms"` out of the status
  line for the sparkline.
- **`cpp/net/`** — `UdpSocket` (BSD sockets, host-byte-order `NetAddr`,
  `ParseNetAddr` supplies the default port 47777), `SourceQuery` (pre-session
  LIST_SOURCES exchange, ~3 s with retries), `NetInfo` (`getifaddrs` IPv4 list with
  friendly interface labels for the share screen).

## 5. UX flows as implemented

- **Home (`HomeView.swift`).** Two tiles (Connect / Share) plus "Recent connections"
  from `Recents.swift`: up to 12 entries in `UserDefaults` (tab/newline separated, no
  JSON), most-recent first, with a link label guessed from the IP range (LAN /
  Tailscale CGNAT). Clicking a recent pre-fills the address and opens Connect — it
  never dials directly, so the view-only choice is not skipped. There is **no
  "found on network" section**: LAN discovery was removed project-wide on
  2026-07-27 — connections start from a typed address.
- **Connect (`ConnectView.swift`).** One hero address field (`ip[:port]`, default
  port 47777 filled by C++), a *View only* checkbox, helper panels. Connect runs
  `SessionModel.listSources()` (SourceQuery) and remembers the address; **0 or 1
  sources → stream immediately** (source 0 — a silent/old host is not an error),
  **>1 → `SourcePickerView`**, where selection is radio-style (the C facade streams
  one `sourceId` at a time) and "Allow input" is the inverse of `viewOnly`. There is
  **no password prompt**: the auth layer was removed from core on 2026-07-27
  (trusted-LAN decision) — no client has one anymore.
- **Share (`ShareView.swift`).** One combined screen (no separate session screen):
  permission banners on top; address panel listing every local IPv4 with the *actual*
  bound port appended; viewer panel (connected/not, send fps, Mbps, capture fps, send
  sparkline — the host cannot measure RTT, only the client can); a display grid
  (labeled "Displays" / "Màn hình", every row with the display icon) with
  **live checkboxes** (ticking during a session calls `dha_add_source`, unticking
  `dha_remove_source` — the viewer never drops). Bottom bar: fps 30/60/120, bitrate
  8/20/40 Mbps, port 47777/47778/52000 (all locked while sharing), *Allow input*
  (default on), and two stop levels: **Stop** (keep
  ticks) vs **Stop all** (danger, clears selection). All options persist in
  `UserDefaults`. Start failures surface a reason line (`startError`) instead of
  failing silently. There is no firewall step in the app; the generated Info.plist
  only carries `NSLocalNetworkUsageDescription` for the OS local-network prompt.

## 6. Build, project layout, signing

`make/macos.mk` (macOS-only, guarded by `UNAME`):

- `make build-macos` — `xcodebuild -project client/macos/Deskhub.xcodeproj -target app
  -configuration Debug SYMROOT=out/build/macos build`
- `make release-macos` — same with `Release`
- `make run-macos` — build then `open out/build/macos/Debug/app.app`
- `make dist-macos` — release build signed with Developer ID, notarized, stapled and
  packaged as a dmg; `make verify-macos` checks Gatekeeper accepts it. Full flow and
  the certificate setup are in `16-release-macos.md`.

`MACOS_SIGN` picks the identity: empty (default) = "Apple Development" from the project,
`adhoc` = ad-hoc for CI build checks, `developerid` = the release identity.

Project (`client/macos/Deskhub.xcodeproj`, single target `app`, shared scheme `app`):
Swift 6.0 + gnu++20, deployment target macOS 14.0, bridging header
`app/swift/Deskhub-Bridging-Header.h`. **core/ is built by a run-script phase**
("Build libcore.a (CMake)") that invokes CMake against `core/CMakeLists.txt` into
`out/build/macos-core/<platform>-<config>` on every build (incremental); the app adds
`core/include` to `HEADER_SEARCH_PATHS` and links `-lcore`. Linked frameworks:
VideoToolbox, CoreMedia, AVFoundation, CoreVideo, ScreenCaptureKit, CoreGraphics,
ApplicationServices, AppKit. Bundle id `com.deskhub.macos` — deliberately different
from the iOS app's `com.ios.deskhub`, which also ships to Apple Silicon Macs as
"Designed for iPad"; two apps must not claim one id on one machine.

Signing: `CODE_SIGN_STYLE = Automatic`, identity "Apple Development" for the macOS SDK
("-" as the base fallback), overridden per release mode via `MACOS_SIGN`.
`ENABLE_HARDENED_RUNTIME = YES` in both configurations, which notarization requires.
`app/Deskhub.entitlements` **disables the App Sandbox** — required for CGEventPost into
other apps and the NSEvent global monitor — while keeping the harmless
`network.client/server` keys for a possible future sandboxed client-only build. Note the
third reason listed in that file (binding a fixed UDP port) is *not* actually a sandbox
restriction; `network.server` covers it. Disabling the sandbox is what rules out the Mac
App Store — see `16-release-macos.md` §1.

## 7. Cross-references

01-architecture.md (one-app/two-roles model) · 02-agent.md (host pipeline policy,
IDR-on-demand) · 03-client.md (client pipeline) · 07-input.md (input model, relative
mouse) · 11-platform-transport.md (per-platform capabilities, POSIX socket reuse).

## 8. Verification status — honest notes

- Both roles are **tested and working** (docs/README.md status table).
- No LAN discovery and no password/auth UI — both layers were removed project-wide
  on 2026-07-27 (trusted-LAN decision; see 15-review-todo.md §A1).
- H.264 only (encoder and HELLO codec mask); no HEVC path exists in this app.
- Key map is US-layout for symbol keys (`cpp/input/MacKeyMap.h`, accepted
  limitation).
- e2e latency shown in the HUD is measured at decoder *enqueue*, not at display
  (`VtDecoder.h` caveat).
- Host-side stats show send rate, not viewer-perceived latency — the host cannot
  measure RTT (only the client does, in `ClientLoop.cpp`), so ShareView's sparkline
  charts send fps instead.
