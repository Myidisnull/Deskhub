# 12 — iOS Client

The iOS app is one of the Deskhub client platforms (see `01-architecture.md`, `03-client.md`).
It is **client-only**: it views and controls a desktop host, never shares its own screen. The
current build streams video *and* sends input — touch-driven mouse, a virtual keyboard, and a
shortcut bar — over the standard UDP protocol described in `04-protocol.md`.

Sources live in `client/ios/`:

- `client/ios/app/swift/` — SwiftUI UI, session state, recents.
- `client/ios/app/cpp/` — C facade, `ClientLoop`, `VtDecoder`, POSIX `UdpSocket`/`SourceQuery`.
- `client/ios/Deskhub.xcodeproj` — a single `app` target; `app/` is a
  `PBXFileSystemSynchronizedRootGroup`, so files added on disk join the build automatically.
- `client/ios/fastlane/` — App Store metadata and lanes (see `13-release-mobile.md`).

## 1. Architecture: four layers

```
SwiftUI views (ConnectView / SourcePickerView / StreamView)
    │  read state, call actions
SessionModel (@MainActor @Observable)          — client/ios/app/swift/SessionModel.swift
    │  the only caller of the facade wrapper
DeskhubClient (Swift enum of static funcs)     — client/ios/app/swift/DeskhubClient.swift
    │  plain C calls via Deskhub-Bridging-Header.h
dh_* C facade (DeskhubClient.h / .mm, ObjC++)  — client/ios/app/cpp/
    │  one global session: std::shared_ptr<ClientLoop> g_client + g_mutex
ClientLoop (pure C++) ── reuses core/: ClientSession, Reassembler, Wire, LinkStats, KeyMap
```

- `DeskhubClient.swift` is the single Swift-side gateway; no view calls C directly. It mirrors
  Android's `NativeClient.kt`, and `DeskhubClient.mm` mirrors `JniBridge.cpp`.
- The facade is flat C functions (not an ObjC class) exported through
  `client/ios/app/swift/Deskhub-Bridging-Header.h`. `DeskhubClient.mm` is ObjC++ only because
  the video layer crosses the boundary as a `__bridge void*`.
- `ClientLoop` (`client/ios/app/cpp/ClientLoop.h/.cpp`) is a close port of the Android
  `ClientLoop`; only the decoder (`VtDecoder` instead of `MediaCodecDecoder`) and the render
  target (`AVSampleBufferDisplayLayer` instead of `Surface`) differ. `UdpSocket` and
  `SourceQuery` under `client/ios/app/cpp/net/` are copied from the Android client (both are
  POSIX). `NetAddr` is IPv4-only, host byte order.

### Threading

Three threads per session, documented in `ClientLoop.h`:

- **Main** — SwiftUI; hands the layer over (`SetLayer`), polls phase/stats every 500 ms via a
  `Timer` in `SessionModel.startPolling()` (added to `RunLoop.main` in `.common` mode so it
  keeps firing during scroll tracking).
- **Net** (`ClientLoop::NetThread`) — `recvfrom` with a 10 ms timeout; video-channel packets go
  straight into `deskhub::Reassembler` (hot path, bypassing `ClientSession` except for
  `NotifyVideoPacket`), everything else through `ClientSession::HandlePacket`. It also drains
  the input queue into `ClientSession`, calls `Tick`, requests
  keyframes, plans NACKs, and closes the 1-second stats window.
- **Decode** (`ClientLoop::DecodeThread`) — pops reassembled frames from a bounded queue
  (`kMaxQueuedFrames = 3`; overflow drops the *oldest* frame and flags an IDR request) and
  feeds `VtDecoder`. Net and Decode are separate so a slow decode never stalls `recvfrom` —
  a stalled socket overflows the kernel UDP buffer and causes real loss.

Layer handover is a generation-counted handshake (`winGen_`/`winAckGen_`): `SetLayer` blocks
the caller until the Decode thread acknowledges it has released the old layer.
`dh_set_layer` deliberately copies the `shared_ptr` and calls `SetLayer` *outside* `g_mutex`
so concurrent `dh_phase`/`dh_stop` calls cannot deadlock. `dh_list_sources` blocks up to ~3 s
and must run off the main thread — Swift wraps it in `Task.detached`.

## 2. Build

`core/` is compiled by a Run Script phase in the Xcode `app` target ("Build libcore.a
(CMake)"): it invokes CMake on `core/CMakeLists.txt` with `CMAKE_SYSTEM_NAME=iOS`, the current
SDK/arch/deployment target (iOS 17.0), and `DESKHUB_CORE_TESTS=OFF`, outputting to
`out/build/ios-core/$PLATFORM_NAME-$CONFIGURATION`. The target links `-lcore` from that
directory; header search paths add `core/include`, `platform/include`, and `app/cpp`. CMake is
the single source of truth for the core file list — Xcode keeps no hand-copied list.

Make targets (`make/ios.mk`, macOS only):

- `make build-ios` — `xcodebuild -target app -configuration Debug -sdk iphonesimulator`,
  products under `out/build/ios/Debug-iphonesimulator/app.app`.
- `make release-ios` — same with `-configuration Release`, still Simulator SDK.
- `make run-ios` — builds, boots the Simulator, installs and launches `com.ios.deskhub`.

Per the comments in `ios.mk`, real-device and App Store builds need a signing team and an
archive through Xcode/fastlane — they are not covered by make. See `13-release-mobile.md`.

**On the Mac.** `SUPPORTS_MAC_DESIGNED_FOR_IPHONE_IPAD = YES`, so the same binary that
goes to TestFlight also runs on Apple Silicon Macs (Intel Macs cannot run it) and is
listed in the Mac App Store under "iPhone & iPad Apps". There is no Mac target, no
separate product, and no extra CI job — Mac Catalyst was considered and rejected. The UI
stays the finger-oriented virtual trackpad, so this build is a store-presence play, not a
good Mac client; the native app in `client/macos` is the better client and the only one
with the host role. Details and the trade-off in `16-release-macos.md` §2.
C++ logging (`client/ios/app/cpp/Log.h`) is `fprintf(stderr, ...)`, visible in the Xcode
console and Console.app.

## 3. Connect flow

1. **Address entry** — `ConnectView` (`client/ios/app/swift/ConnectView.swift`): a bare **IP
   address** field and a Connect button, nothing else. There is **no network discovery**; the
   port is the fixed constant `kDeskhubPort` = 47777 filled in by `ParseNetAddr` in the C++
   layer, which **rejects** any string containing `:`. No view-only checkbox — input is always
   shared (all of this was brought in line with the other clients on 2026-07-27).
2. **Source query** — `SessionModel.connect()` runs `DeskhubClient.listSources` (blocking
   `QuerySources`, LIST_SOURCES → SOURCE_LIST) in `Task.detached`. Every source is a shared
   display (window sources were removed 2026-07-27; rows use the "display" icon). More than
   one source shows
   `SourcePickerView` (radio-style rows, "Start viewing" button); exactly one — or a silent
   host, treated as "old host / single source", not an error — skips straight to source 0.
3. **Switching display mid-session** — the host shares *every* display, so `SessionModel`
   keeps the whole source list and `StreamView` has a `Display` button (shown only when there
   is more than one) that calls `switchSource`: `dh_stop` + `dh_start` with a different
   `sourceId`, without leaving the stream screen. There is no "change source" protocol
   message and none is needed — each (client, source) pair is already its own session.
   (The **Recents** list of up to 12 machines was deleted 2026-07-27; only the last address is
   remembered, pre-filled into the field.)
4. **No password step** — the auth layer (passwords, Keychain credentials, device tokens)
   was removed project-wide on 2026-07-27 (trusted-LAN decision, see 15-review-todo.md
   §A1); `dh_start` takes just the address and `sourceId`, `clientId` is random per
   session inside the native layer.

## 4. Streaming path

```
UdpSocket.RecvFrom → Reassembler (core, FEC + NACK) → decQueue_ (≤3)
  → VtDecoder (Annex-B → AVCC, CMSampleBuffer) → AVSampleBufferDisplayLayer
```

`VtDecoder` (`client/ios/app/cpp/decode/VtDecoder.h/.mm`) does **not** run an explicit
`VTDecompressionSession`: it enqueues H.264 `CMSampleBuffer`s directly into an
`AVSampleBufferDisplayLayer`, which hardware-decodes and composites itself (the iOS analogue
of Android's `releaseOutputBuffer(..., true)`). Details:

- **Annex-B → AVCC**: the stream is Annex-B with SPS/PPS repeated in-band on every IDR.
  `ParseAnnexB` splits NALs; SPS/PPS build a `CMVideoFormatDescription`
  (rebuilt only when the parameter bytes change); slice NALs get 4-byte length prefixes.
  Frames before the first IDR (no parameters yet) are silently skipped — not an error.
- **Low latency**: every sample gets `kCMSampleAttachmentKey_DisplayImmediately`; the stream
  has no B-frames. If the layer is not `readyForMoreMediaData` the frame is dropped rather
  than queued. A layer in `Failed` status (typical after returning from background) is
  flushed and `Decode` returns false, which makes `ClientLoop` rebuild the decoder and
  request an IDR.
- **Format changes**: host RECONFIG (`onReconfig`) stores the new size and sets
  `rebuildDecoder_`; the Decode thread shuts the decoder down and re-inits lazily once it has
  both a layer and negotiated dimensions. The host sends an IDR with RECONFIG.
- **IDR requests** funnel through one place in `NetThread` with a logged reason: packet loss,
  waiting-for-IDR, decoder failure, or frame-queue overflow.

The UI layer is `VideoLayerView` (`client/ios/app/swift/VideoLayerView.swift`), a
`UIViewRepresentable` whose `UIView.layerClass` is `AVSampleBufferDisplayLayer`
(`videoGravity = .resizeAspect`). `StreamView` sizes it with `.aspectRatio` from the
negotiated `videoWidth/Height`.

**Stats HUD**: `NetThread` builds a one-line summary every second
(`fps  Mbps  loss %  RTT ms  e2e ms`); `SessionModel.poll()` reads it via `dh_status_line` and
`StreamView` prints it as one line of text at the top of the control panel. (It used to also be
parsed for RTT and drawn as a sparkline; that went with the design system on 2026-07-27.)

**Screen layout (2026-07-29)**: the video is full-bleed (`.ignoresSafeArea()`), and everything
else lives in one collapsible control layer pinned bottom-trailing — collapsed it is a single
44 pt round button, expanded it is an `.ultraThinMaterial` panel with the address + status
line, the hotkey row, Keyboard/Display/End and an ✕ to collapse. It replaced three stacked rows
(status bar / video / button bar), which cost the frame those two bar heights at all times.

The whole `ZStack` takes `.ignoresSafeArea()`, so the stack's frame *is* the full screen and a
`.bottomTrailing`-aligned child would sit under the home indicator (and run into the notch in
landscape). The insets are therefore read from a `GeometryReader` placed **outside** the
ignoring subtree — inside one, `proxy.safeAreaInsets` is always zero — and applied by hand,
differently per layer:

- **control layer + status overlay** — padded on all four edges, so buttons and text always
  clear the notch and the home indicator. Those insets include the keyboard region, so opening
  the soft keyboard nudges the panel up while the frame behind it stays put.
- **video** — padded on the **leading/trailing edges only**. In landscape the notch sits on a
  side edge and physically hides pixels, so a full-bleed frame loses the edges of the remote
  desktop (very visible with an ultrawide host: a 3440×1440 source fills the width, so its left
  and right columns disappear). Top and bottom stay unpadded: the only thing there is the home
  indicator, which draws *over* the picture without hiding it. On a 13 Pro Max in landscape
  that makes the frame 832×348 pt instead of 926×388 — about 10% smaller, in exchange for
  nothing being swallowed by the notch. The e2e figure
is measured at *enqueue* time, not at display time — a known caveat noted in `VtDecoder.h`
(`lastRenderedPtsUs`).

## 5. Input

All input funnels through `SessionModel`. There is no gate behind it any more — the `viewOnly`
flag was removed 2026-07-27; the funnel stays so views never touch the facade directly. The
C++ side queues events under `inputMutex_`; the Net thread
drains them into `ClientSession`, which sequences and redundantly retransmits them
(`InputSender`, see `07-input.md`). Any input sets `wantFocus_`, so the host receives
SET_FOCUS — since 2026-07-27 the host takes no action on `true` (it used to raise the shared
window); only the `false` edge matters, releasing held keys. Input only takes effect while
STREAMING.

- **Touch → mouse** — `TouchInputView` (`client/ios/app/swift/TouchInputView.swift`) is a
  *trackpad*, not direct touch: a visible cursor (SF Symbol `cursorarrow`) is moved by pan
  deltas. It is stored in *normalized* 0..1 video coordinates and only rendered through the
  current video rect, so zooming/panning the view cannot move it on the host; sending it is a
  multiply by 65535. Gestures: drag = move cursor; single tap = left click (waits for the
  double-tap window to fail); double tap = right click; long-press-then-drag = hold left
  button and drag, released on lift. A move is re-sent immediately before every click so
  clicks land under the visible cursor. The overlay fills the whole screen — letterbox
  included — minus `blockedRect`, the frame of the control layer measured by `StreamView` in
  `.global` coordinates: `point(inside:)` returns false there, so UIKit never hit-tests into
  the view and its gesture recognizers never see those touches. A finger landing on the
  panel therefore cannot jog the cursor. It is mounted whenever the session is streaming.
- **Pinch zoom** (2026-07-30) — two fingers pinch to scale the frame 1×..5× and drag to move
  to another region; a tap on the zoom pill (in the control layer, bottom-right) goes back to
  1×. `ViewTransform` (`ViewTransform.swift`) owns the whole model: `frame(in:aspect:)`
  returns the displayed video rect — aspect-fit, scaled about the viewport centre, then panned
  with the pan clamped so no black gap opens — and `apply(...)` folds one gesture into an
  absolute (zoom, pan). Photo-viewer semantics: the point between the fingers stays put and
  the frame grows around it (`pan' = (centroid - centre)(1 - ratio) + pan * ratio + panDelta`),
  with the pinch and the two-finger pan recognizers running together so a pinch that drifts
  drags the picture along.

  **The zoom is a transform, not a layout change**, mirroring Android. `StreamView` lays the
  video layer out at the 1× rect (`ViewTransform.baseFrame`) and puts `.scaleEffect(zoom,
  anchor: .topLeading)` + `.offset` on top. Setting the scaled frame directly instead changes
  the layer's bounds, which costs a layout pass and makes the layer rebuild its contents on
  every frame of the pinch; a transform is just a matrix, so the compositor samples the
  decoded buffer straight — no relayout and no loss of sharpness. The trackpad overlay itself
  is never scaled.

  The frame never moves *on its own*. (The first cut had the cursor drag it along when the
  cursor reached a screen edge; in practice that threw away the region you had just zoomed
  into the moment you touched the screen, because the cursor was somewhere off-view.) The
  cursor is clamped to the *visible* part of the video — `videoRect ∩ bounds`,
  `clampToVisible` — rather than the whole frame, so it can never end up off-screen. The clamp
  never sends anything: every click re-sends the cursor position first anyway.

  **One finger has two jobs, so there is a switch.** Once zoomed, the thing you most want is a
  one-finger swipe to look somewhere else — but one finger is the trackpad, and the swipe
  itself carries no hint of which was meant. `panMode` decides: on, one finger moves the
  frame; off, one finger moves the pointer. It is a "Pan"/"Pointer" pill in the control layer
  (`ZoomControls`), shown only while zoomed (at 1× there is nothing to move), and it flips to
  Pan automatically on zooming in and back to Pointer at 1×. While Pan is on, *every*
  one-finger gesture is silent, taps included — misfiring a click on the host while you are
  swiping to look around is worse than having to tap the switch. Pinch and the two-finger pan
  work in both modes.

  Pinch and the two-finger pan are the only recognizers allowed to run simultaneously — the
  one-finger gestures keep UIKit's default exclusion, so a long-press drag cannot survive a
  zoom.
- **Virtual keyboard** — `KeyInputView` (`client/ios/app/swift/KeyInputView.swift`) is an
  invisible `UIKeyInput` view (ASCII keyboard, autocorrect off) toggled by the control panel's
  Keyboard button; a transparent accessory bar adds a "Done" dismiss button. Each typed scalar goes to
  `dh_char_tap`; `ClientLoop::QueueCharTap` uses core `CharToKeyChord` (`KeyMap.h`, US layout)
  to emit `[Shift↓] key↓ key↑ [Shift↑]`; backspace is sent as codepoint `0x08`. Non-ASCII
  characters are silently dropped.
- **Shortcut bar** — `StreamView`'s `kHotkeys` pill row supplies keys the iOS keyboard lacks:
  Esc, Tab, Enter, arrow keys, Del, Ctrl+C, Ctrl+V (Windows VK + scancode, bit 8 = E0 flag).
  Plain keys use `dh_key_tap`, combos use `dh_key_chord` (modifier held around the main key).
  Alt+Tab/Win are deliberately excluded (a rule from the per-window sharing era, kept because
  they are rarely useful from a hotkey bar).
- **Tap timing** — key-down is sent immediately; key-up is scheduled `kTapHoldUs` (50 ms)
  later in `delayedInput_`, so games polling the keyboard per frame still see the press.

## 6. Lifecycle

- `StreamView.onAppear` disables the idle timer and starts the 500 ms poll;
  `onDisappear` re-enables it, dismisses the keyboard, calls `dh_set_layer(NULL)`, and stops
  polling.
- `scenePhase` handling: on `.background` the layer is released (`dh_set_layer(NULL)` blocks
  until the Decode thread lets go); on `.active` it is re-attached. While layerless the Decode
  thread drops frames; re-attachment triggers a decoder rebuild plus an IDR request. The
  session itself keeps running in the C++ threads.
- `SessionModel.disconnect()` (End button, ended-overlay Back) calls
  `dh_stop`: `ClientLoop::Stop` raises `quit_`, joins both threads, and the Net thread sends
  a one-shot BYE so the host frees the session immediately. Host BYE / timeout / socket error
  set `endReason`, phase `.ended`, and `StreamView` shows the ended overlay.

## 7. Known limitations (as coded)

- **No scroll**: no gesture maps to mouse wheel; only move/left/right/drag exist. (Pinch is
  taken by view zoom, which is view-side only and never reaches the host.)
- **Relative mouse is a stub**: `dh_mouse_move_rel` / `QueueMouseMoveRel` exist for an
  FPS-style pointer-lock mode, but no UI calls them (the "Lock" button was removed).
- **US-ASCII typing only**: `CharToKeyChord` covers the US layout; other characters are
  dropped in `QueueCharTap`.
- **e2e latency is approximate**: measured at sample enqueue, not on-glass
  (`VtDecoder::lastRenderedPtsUs` caveat); switching to `VTDecompressionSession` would be
  needed for exact timing.
- **Simulator-only make targets**: device/App Store builds require manual signing
  (`make/ios.mk`, `13-release-mobile.md`).
- **One global session** (`g_client` in `DeskhubClient.mm`); IPv4 only (`NetAddr`);
  no host discovery — addresses are typed or picked from recents.
