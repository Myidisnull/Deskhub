# 08 — Android Client

The Android app (`client/android/`) is a client-only viewer and controller: it connects to a
Deskhub host, decodes the H.264 stream with the device's hardware decoder, and sends mouse and
keyboard input back. It reuses the shared protocol/session code in `core/` unchanged (see
01-architecture.md); everything Android-specific is a thin Kotlin UI plus a small C++ layer.
The Windows client described in 03-client.md is the reference implementation this port follows.

## Architecture

Three layers, one crossing point:

```
MainActivity / StreamActivity (Kotlin, Jetpack Compose)
        │  every native call goes through this single object
NativeClient.kt  ──JNI──  JniBridge.cpp
        │
ClientLoop (C++)  →  core/ (ClientSession, Reassembler, Wire, KeyMap, LinkStats)
        ├── net/UdpSocket, net/SourceQuery
        └── decode/MediaCodecDecoder
```

- `app/src/main/java/com/deskhub/app/NativeClient.kt` is the only Kotlin file allowed to declare
  `external fun`s. JNI binds by string at runtime, so the names must match
  `Java_com_deskhub_app_NativeClient_*` in `app/src/main/cpp/JniBridge.cpp` exactly — a mismatch
  compiles fine and dies with `UnsatisfiedLinkError`.
- `JniBridge.cpp` is deliberately thin: type conversion plus lifetime of one global
  `std::unique_ptr<ClientLoop>` (`g_client`) and one held `ANativeWindow*` (`g_window`). A global
  session generation counter (`g_generation`, returned by `nativeStart`) lets a late `onDestroy`
  of an old `StreamActivity` avoid killing a session a new activity just opened.
- `app/src/main/cpp/ClientLoop.{h,cpp}` is the C++ heart, a close port of
  `client/windows/ClientLoop.cpp`. It wires `UdpSocket`, `deskhub::ClientSession`,
  `deskhub::Reassembler`, and `MediaCodecDecoder` into one session.

### Threads

- **Main (UI) thread** — all Compose UI; calls `Start`/`Stop`/`SetWindow`, polls status every
  500 ms (a `LaunchedEffect` in `StreamActivity.StreamScreen`) instead of C++ calling back into
  the JVM. `NativeClient.listSources` is a `suspend fun` that hops to `Dispatchers.IO` because
  the underlying `nativeListSources` blocks up to ~3 s.
- **Net thread** (`ClientLoop::NetThread`) — `recvfrom` with a 10 ms timeout; video packets go
  straight into the `Reassembler` (bypassing `ClientSession` on the hot path), everything else
  through `ClientSession::HandlePacket`; drains the input queue, runs `session.Tick`, sends
  FEEDBACK/NACK, closes per-second stat windows.
- **Decode thread** (`ClientLoop::DecodeThread`) — pops reassembled frames from a bounded queue
  (`kMaxQueuedFrames = 3`, oldest frame dropped on overflow so the Net thread never blocks),
  feeds `MediaCodecDecoder`, and services Surface handoffs.

Surface handoff is the one place a thread blocks on another: `ClientLoop::SetWindow` bumps a
generation counter (`winGen_`) and waits until the Decode thread acknowledges (`winAckGen_`)
that the codec released the old `ANativeWindow` — destroying a Surface the codec still renders
into is a use-after-free. A `decodeExited_` flag is the anti-hang escape hatch.

## Build system

- `client/android/settings.gradle.kts` — single `:app` module, project `DeskhubAndroid`.
- `client/android/build.gradle.kts` — AGP 9.3.1 (Kotlin is built into AGP 9; no separate
  `kotlin.android` plugin) plus the `org.jetbrains.kotlin.plugin.compose` compiler plugin.
- `client/android/app/build.gradle.kts` — `namespace com.deskhub.app` (fixed: JNI symbol names
  depend on it), `applicationId` defaults to `com.manhpham.deskhub` but is injected by fastlane
  via `-PapplicationId`/`-PversionName`/`-PversionCode` for releases (see 13-release-mobile.md);
  `minSdk 24`, `targetSdk 36`, `compileSdk 37`, NDK 26.1, ABIs `arm64-v8a` + `x86_64`,
  `-DANDROID_STL=c++_static` (the app ships exactly one `.so`).
- `app/src/main/cpp/CMakeLists.txt` — invoked by Gradle/NDK, builds `libdeskhub.so`. It walks
  six directories up to the repo root and `add_subdirectory`s `core/` and `platform/` directly,
  so the shared C++20 code is compiled by the NDK toolchain with no copies. It also links
  `android`, `mediandk`, `log`, and passes `-Wl,-z,max-page-size=16384` so the library loads on
  16 KB-page devices (NDK r26 still aligns to 4 KB by default).
- `make/android.mk` — `make build-android` (`gradlew assembleDebug`), `make release-android`
  (`assembleRelease`), `make run-android` (`installDebug` + `adb shell am start`). Building
  needs only the SDK, no device.
- **Signing**: release builds are signed only when the `KEYSTORE_FILE`/`KEYSTORE_PASSWORD`/
  `KEY_ALIAS`/`KEY_PASSWORD` environment variables are set (CI/fastlane); without them
  `release-android` produces an unsigned APK. Debug builds use the default debug key.

## Connect flow

`MainActivity` models the flow as a `sealed interface Step`: `Address` → `Querying` →
`Picking`. There is no network discovery — the user types a bare **IP address** (the port is
the fixed constant `kDeskhubPort` = 47777 in `cpp/net/UdpSocket.h`; `ParseNetAddr` rejects any
string containing `:`). The last address typed is written to `SharedPreferences` and pre-filled
next launch; that is the only memory left, since the **Recents** list (`ui/Recents.kt`, up to 12
machines with LAN/Tailscale labels) was deleted 2026-07-27 along with the on-screen help text.

Connect triggers `NativeClient.listSources`, which runs `QuerySources`
(`cpp/net/SourceQuery.cpp`): a pre-session UDP exchange that resends LIST_SOURCES every 500 ms
for up to 3 s and accepts the first SOURCE_LIST from the queried host (see 04-protocol.md).
Zero or one source skips the picker (old hosts don't know LIST_SOURCES); multiple sources show
`SourcePickerScreen` with radio-style rows. A `seq` on `Step.Querying` discards results from a
stale, non-cancellable query after Back + reconnect.

`StreamActivity` is then started with `addr` + `source` extras and calls
`NativeClient.nativeStart(addr, sourceId)` — the `clientId` is random per session inside the
native layer. The **whole source list** rides along as four parallel arrays
(`srcIds`/`srcW`/`srcH`/`srcNames`), which is what lets the stream screen switch displays
without a second 3-second query.

**Switching display mid-session** (added 2026-07-27, now that hosts share every display): a
`Display` button in the bottom bar — shown only when the host published more than one source —
opens a radio dialog and calls `StreamActivity.switchSource`. The protocol has no "change
source" message and does not need one: each (client, source) pair is already an independent
session, so switching is `nativeStop` + `nativeStart` with a different `sourceId`. The
`SurfaceView` is *not* recreated — `JniBridge` holds `g_window` independently of session
lifetime and `nativeStart` re-attaches it — so the swap costs one handshake and no black flash.
`StreamScreen` keys its polling `LaunchedEffect` on the session generation so stats and any
end-reason from the closed session are cleared rather than lingering. There is no password step: the auth layer was removed project-wide on
2026-07-27 (trusted-LAN decision, see 15-review-todo.md §A1).

A debug-only shortcut: `am start ... --es addr 10.0.2.2` opens `StreamActivity` directly
(guarded by `FLAG_DEBUGGABLE` because `MainActivity` is exported).

## Streaming path

UDP datagram → `Reassembler` (fragment/FEC reassembly, loss accounting) → frame queue →
`MediaCodecDecoder` → SurfaceView. Video pixels never touch Compose or the JVM.

`decode/MediaCodecDecoder.cpp` configures an `AMediaCodec` H.264 decoder directly with the
`ANativeWindow`, so `AMediaCodec_releaseOutputBuffer(..., true)` *is* the render — zero-copy
through the hardware composer (the reason `StreamActivity` uses `SurfaceView`, not
`TextureView`). The `"low-latency"` format key is set as a string so the `.so` still loads
before API 30. On the first frame after each codec (re)build, SPS/PPS preceding the first VCL
NAL are submitted separately under `BUFFER_FLAG_CODEC_CONFIG` (`FirstVclOffset`) for decoders
that require it. `Decode()` returning false means "codec broken": the Decode thread shuts it
down, sets `decodeFailed_`, and the Net thread requests an IDR.

Reconfiguration: the `onReconfig` callback stores the new negotiated size and sets
`rebuildDecoder_` — unlike the Windows `MfDecoder`, MediaCodec is torn down and rebuilt on a
resolution change (the host sends an IDR alongside, so nothing is lost). All keyframe-request
paths (reassembler loss, `WaitingForIdr`, decode failure, queue overflow) funnel into
`session.RequestKeyframe()` with `[DIAG]` logging per 09-diagnostics.md.

Stats surfaced to the UI: `nativeStatusLine` returns the one-per-second line built in
`ClientLoop::NetThread` (`fps / Mbps / loss % / RTT / e2e`), printed as a plain line of text at
the top of the control panel. The stream screen is **full-bleed video plus one collapsible
control layer** since 2026-07-29: collapsed it is a single 48 dp round button in the
bottom-right corner, expanded it is a translucent bottom panel holding the address + status
line, the hotkey row, Keyboard/Display/End and an ✕ to collapse again. (Before that it was
three stacked rows — status bar, video `weight(1f)`, button bar — which cost the frame those
two bar heights permanently; before *that* the bars floated over the video with no way to
hide them.) The control layer takes `safeDrawingPadding()` (which includes the IME, so the
panel rides above the soft keyboard); the video takes only
`windowInsetsPadding(WindowInsets.displayCutout.only(Horizontal))` — in landscape the cutout
sits on a side edge and *hides* picture (the edges of an ultrawide desktop vanish), while the
top/bottom gesture bar merely draws over it, so those edges stay full-bleed. The panel rounds
its corners with `background(color, shape)` and **not** `clip(shape) + background(color)`: in
landscape the vertical room is barely more than the panel needs, and `clip` sliced the bottom
off the last button row.

Note when testing on an emulator: unless the image is Android 15+ (or the activity opts in
explicitly), the window is *not* edge-to-edge — the system already excludes the cutout and the
navigation bar, `WindowInsets.safeDrawing` reads all-zero, and the cutout padding above is a
no-op. Verifying it needs an edge-to-edge device/image plus
`cmd overlay enable com.android.internal.display.cutout.emulation.tall`. (It used to also be parsed for RTT and drawn as a 60-sample
sparkline; that went with the design system on 2026-07-27 — the numbers are still all there in
the line itself.) `nativeVideoWidth/Height` drive the letterbox aspect ratio. Full per-second stats and `[DIAG]`
events go to logcat, tag `Deskhub` (`cpp/Log.h`; `adb logcat -s Deskhub`).

## Input

All input funnels through `NativeClient`: the raw `external` functions stay private and the
public wrappers (`keyTap`, `keyChord`, `mouseMove`, `mouseButton`, `charTap`, `mouseMoveRel`)
are the only door down to JNI. They no longer gate anything — the view-only checkbox and
`NativeClient.viewOnly` were removed 2026-07-27; the single door is kept so any future rule
still has exactly one place to live. On the C++ side, `ClientLoop::Queue*` methods push `deskhub::InputEvent`s into `inputQueue_`
under a mutex; the Net thread drains the batch each loop into `ClientSession::QueueInput`,
which sequences and redundantly sends them via the core `InputSender` (see 07-input.md), and
calls `SetFocused(true)` once any input has been sent. (The host no longer raises anything on
`SET_FOCUS(true)` — that went with window sharing, removed 2026-07-27; only the `false` edge
matters, releasing held keys.)

- **Trackpad** (`TrackpadOverlay` in `StreamActivity.kt`) — laptop-touchpad semantics: an
  always-visible drawn cursor (`CursorArrow`) moves by *delta*, never jumps to the touch point.
  Tap = left click at the cursor, double-tap = right click, long-press-then-drag = left-button
  drag (mutually exclusive with plain drags by construction). The overlay fills the whole
  screen — letterbox included — but the control layer sits on top of it and swallows every
  pointer event that lands on it (`Modifier.consumeTouches`, a `pointerInput` that consumes on
  the Main pass so child buttons still work), so a finger landing on the panel — or on the gap
  between its buttons — cannot jog the cursor. The cursor itself is clamped to the
  actual video rect and positions are normalized to 0..65535 within it (`sendMouseMove` →
  `QueueMouseMoveAbs`). It is mounted whenever the session is streaming.
- **Virtual keyboard** (`KeyInputView.kt`) — an invisible 1 dp view that holds IME focus and
  captures both input paths: `commitText`/`deleteSurroundingText` on a dummy
  `BaseInputConnection` (Gboard-style IMEs) and raw `onKeyDown` (physical/Bluetooth keyboards).
  `VISIBLE_PASSWORD + NO_SUGGESTIONS` forces per-key commits with no composition. Each
  codepoint goes through `nativeCharTap` → `QueueCharTap`, where core `CharToKeyChord`
  (US layout) expands it into `[Shift↓] key↓ key↑ [Shift↑]`; non-ASCII characters are silently
  dropped.
- **Hotkey row** — the `kHotkeys` list in `StreamActivity.kt` (Esc, Tab, Enter, arrows, Del,
  Ctrl+C, Ctrl+V) sends Windows virtual-key codes + scancodes (bit 8 = E0 flag) via
  `keyTap`/`keyChord`. Alt+Tab and the Win key are intentionally excluded (originally because
  they moved focus off the shared window under the old per-window sharing; still left out as
  rarely useful from a hotkey bar).

Tap releases are scheduled `kTapHoldUs` (50 ms) after the press (`delayedInput_`) so games that
poll the keyboard per frame actually see the key held.

## Lifecycle

- `surfaceCreated` → `nativeSetSurface(holder.surface)`; `surfaceDestroyed` →
  `nativeReleaseSurface(holder.surface)`, which blocks until the decoder lets go and compares
  Surface *identity* so a late callback from an old activity cannot steal the new session's
  window. `FLAG_KEEP_SCREEN_ON` prevents the screen (and therefore the Surface and session)
  from dying mid-view.
- **Backgrounding ends the session**: `StreamActivity.onStop` calls `finish()` unless the
  activity is finishing or changing configuration — the protocol has no pause, and without this
  the Net thread would keep receiving full bitrate invisibly. Rotation survives
  (`configChanges` in `app/src/main/AndroidManifest.xml`).
- `onDestroy` calls `nativeStop(session)` with the generation from `nativeStart`, stopping only
  the session this instance created. There is no automatic reconnect: `PHASE_ENDED` shows
  `EndedOverlay` with `nativeEndReason`, and the user reconnects from `MainActivity`. On
  session end the Net thread sends BYE best-effort so the host frees the slot immediately.

## UI system (`ui/` package)

**There is no `ui/` package any more.** On 2026-07-27 the whole bespoke design system was
deleted in stages, on request: first the language and theme switches (`AppState.kt`,
`Strings.kt` + `tr(key)`, `SunIcon`/`MoonIcon`), then — "trông cơ bản thôi, không cần màu mè" —
`Tokens.kt`, `Components.kt` and the rest of `Icons.kt`, and finally every line of on-screen
help text plus `Recents.kt`.

Both screens now use **stock Material 3** (`MaterialTheme(colorScheme = darkColorScheme())`,
`OutlinedTextField`, `Button`, `OutlinedButton`, `RadioButton`, `CircularProgressIndicator`,
`Text`) with English literals inline. Around 1,400 lines of UI code went away; the four
remaining Kotlin files are `MainActivity` (~290), `StreamActivity` (~600), `NativeClient`
(~220) and `KeyInputView` (~94).

What was **kept** because it is functional, not decoration: the SurfaceView + `aspectRatio`
letterbox, `TrackpadOverlay` with its drawn `CursorArrow` (delta cursor, tap / double-tap /
long-press-drag), the invisible `KeyInputView` that holds IME focus, and the horizontally
scrolling hotkey row. The RTT sparkline went with the design system — the status line still
shows the same numbers as text.

## Known limitations

- **Relative mouse mode is a stub**: `nativeMouseMoveRel`/`QueueMouseMoveRel` (FPS
  pointer-lock, the Windows client's F9 mode) exist end-to-end but no UI calls them — the Lock
  button was removed.
- Virtual-keyboard typing is limited to US-ASCII; anything `CharToKeyChord` cannot map is
  dropped. No scroll-wheel or pinch-zoom gesture exists.
- No host discovery (no mDNS/broadcast); the address is typed by hand (the last one is pre-filled).
- One session at a time by design: a single global `ClientLoop` behind JNI.
- No pause/resume — backgrounding terminates the session (see Lifecycle).
- IME auto-dismiss tracking (keyboard button state) requires API 30+; older devices keep the
  button latched until pressed again.
- View-only is enforced client-side only, in `NativeClient`.
- H.264 only (`hello.codecMask = kCodecMaskH264`); no audio path exists in the app.
- `make/android.mk`'s header still says no `signingConfig` exists; the Gradle file has since
  added the env-driven release signing described above.
