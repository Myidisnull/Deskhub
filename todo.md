# Deduplication backlog — round 2

Audit of `client/`, `core/`, `platform/` after the first round landed
(`Wire`, `Packetizer`, `HostRouter`, `ClientPump`, `ViewFit`, `ClientNetLoop`,
`SourcePipelineState` are already shared). What is left is the two big loops —
`ClientLoop` and `AgentLoop` — which are still copied 3–5 times.

Ordered by benefit/risk. Line counts are real, measured with `diff`/`wc`.

Ground rules while working through this:

- `core/` stays free of OS and third-party headers, and must not depend on `platform/`.
- Anything new in `core/` gets a test under the matching `core/tests/` subdirectory.
- `make test` and `make lint` before calling an item done.
- Apple targets compile `client/<os>/app` through Xcode filesystem-synchronized groups
  and link `libcore.a` + `libplatform.a` from a CMake pre-build phase. A file added to
  `platform/` reaches macOS **and** iOS for free; a file under `client/apple/` would need
  a new synchronized group in both `.xcodeproj`.
- Only Windows and Android can be compiled on the current dev machine. Linux, macOS and
  iOS changes are written blind and must be verified on their own hosts.

---

## Tier 0 — Unblockers (cheap, no behaviour change)

- [x] **0.1 `client/windows` uses `std::printf` instead of `LOGI`/`LOGW`/`LOGE`.**
      100 call sites across 13 files; not one file in `client/windows` includes
      `deskhubp/diag/Log.h`. CLAUDE.md requires the shared helpers. On Windows `LOGI`
      already expands to `printf` plus a `[Deskhub]` prefix and a newline, so the change is
      mechanical and the only visible difference is the prefix. This has to happen before
      the `AgentLoop` bodies can be merged (Tier 2), because the log calls are otherwise
      the main textual difference between the three copies.
      *Done — all 100 call sites converted across 13 files, each classified as info, warn or
      error rather than blanket-`LOGI`; `Log.h` added to every file. Windows compiles.*

- [x] **0.2 `DisplayEnum` for Linux lives in the wrong layer.** — *decided: leave it.*
      `client/linux/cpp/capture/DisplayEnumLinux.cpp` implements `deskhubp::ListDisplays()`
      — a `platform/` API — from inside `client/`. The Windows and macOS implementations
      sit correctly in `platform/src/media/`.
      *Not moved, deliberately. Enumerating displays on Linux means opening a portal
      screen-cast session, so the implementation needs GIO/D-Bus and PipeWire. Moving it to
      `platform/` would pull those into every target that links `platform`, including
      `core_tests`. `platform/CMakeLists.txt` already compiles no `DisplayEnum*` source on
      Linux — the API is declared there and the symbol is supplied at link time by the Linux
      client. That seam is intentional; this entry records it so it is not "fixed" later.*

- [x] **0.3 `MacKeyMap` uses raw VK literals.**
      `client/macos/app/cpp/input/MacKeyMap.cpp` spells virtual keys as `0xBD`, `0xA0`,
      `0x5B`… while `client/linux/cpp/input/LinuxKeyMap.cpp` uses the named `kVk*`
      constants from `core/include/deskhub/input/VirtualKeys.h`. Both build the same
      `deskhub::ScancodeEntry<uint16_t>` table. Switch macOS to the named constants.
      *Done — the whole table plus `ModifierOf` now use `kVk*`; F-keys are `kVkF1 + n` and the
      numpad is `kVkNumpad0 + n`, so the two maps read the same way. Values are unchanged.
      Not compiled here — needs a build on macOS.*

---

## Tier 1 — Pure logic still sitting in `client/`, belongs in `core/`

- [x] **1.1 Encode-size clamping is implemented three different ways.**
      `client/linux/cpp/AgentLoop.cpp:180-235` does `FitStreamSize` + even-clamp (`&~1u`)
      + a minimum-size check + an fps gate. `client/macos/app/cpp/AgentLoop.cpp:183` only
      does the even-clamp. Windows does something else again. Same problem, three
      behaviours. Lift into `core/include/deskhub/control/StreamSize.h` as a single
      `ClampEncodeSize(native, want, maxDim)` returning a `StreamSize`, with tests.
      *Done — `deskhub::ClampEncodeSize()` in `core/control/StreamSize.h` returns an
      `EncodeSize` carrying the size and a `tooSmall` flag. Wired into Windows and Linux.
      macOS deliberately still sizes in the capture layer: ScreenCaptureKit is told the
      target size through `SetClientSize`/`SetQuality`, so `nativeW`/`wantW` are never
      published there and running the clamp would fight the capture engine. macOS uses the
      shared floor constants only.*

- [x] **1.2 `kMinEncodeW` / `kMinEncodeH` (160 / 64) are redeclared per platform.**
      Fold into the `core` header from 1.1.
      *Done — `deskhub::kMinEncodeWidth` / `kMinEncodeHeight`; all three copies removed.*

- [x] **1.3 Per-source fps gate.**
      Only Linux has it (`AgentLoop.cpp:225-231`). It is arithmetic on timestamps, so it
      belongs in `core` and should then apply everywhere. Small state struct + test.
      *Done — `deskhub::FrameGate` in `core/control/FrameGate.h`, wired into Windows and
      Linux. Writing the test surfaced a latent bug in the original Linux code: it used a
      stored timestamp of `0` to mean "no reference frame", so a source whose first frame
      landed on `t=0` was never gated. `FrameGate` stores `timestamp + 1` internally, so
      `t=0` is a real reference. macOS is rate-limited by ScreenCaptureKit and needs no gate.*

- [x] **1.4 `DiagEncode` wrapper is written three times with three signatures.** — *dropped.*
      Measures the encode, feeds `diag.encMs`, logs `enc_fail`.
      *Tried and reverted. `core/` has neither a clock nor a logger, so a shared version has
      to take the elapsed time and a failure callback as parameters — three template
      arguments to remove five lines per platform, and each platform's encoder takes a
      different frame handle anyway. The wrapper is left where it is; the surrounding loop
      that actually mattered is shared via 2.2.*

- [x] **1.5 `PublishStatus` — `SourcePipelineState` → `AgentSourceStatus`.**
      `client/linux/cpp/AgentLoop.cpp:363-397` and the macOS/Windows equivalents differ
      only in the `zeroCopy` flag. Pure struct mapping → `core`.
      *Done — `deskhub::MakeSourceStatus()` + `MakeSourceInfo()` in `core/session/HostRouter.h`.
      The formatted peer address stays a caller concern (`NetAddr` is a `platform/` type) and
      arrives via `StatusExtras`. All three platforms use it. One behaviour change, deliberate:
      the address is now cleared when no peer is adopted, so a stale address cannot survive a
      disconnect into the UI.*

- [x] **1.6 `InputInjector::Apply()` dispatch.**
      `client/windows/cpp/input/InputInjector.cpp:113`,
      `client/linux/cpp/input/InputInjector.cpp:138`,
      `client/macos/app/cpp/input/InputInjector.mm:153` all run the same sequence:
      gate on `PressedInputTracker::Gate` → `switch (e.type)` → record in the tracker →
      `CountApplied`. Only the five `Send*` calls are OS-specific. Lift the dispatch into
      a `core` template parameterised on the backend.
      *Done — `deskhub::InputApplier<Backend, NativeKey>` (CRTP) in
      `core/input/InputApplier.h` owns the host-wins gate, the event switch and the applied
      counter; the backend supplies `SendKey`/`SendButton`/`SendMoveAbsolute`/
      `SendMoveRelative`/`SendWheel` plus `OnLocalUserTookOver`/`OnLocalUserIdle`/`ReleaseAll`.
      The five `Send*` signatures were normalised across the three platforms to make this
      possible (Windows grew a real `SendWheel`, Linux and macOS grew the unused `scan`
      parameter). Tested against a fake backend.
      This fixed a real bug on Linux: when the local user started typing, Linux logged the
      takeover but never called `ReleaseAll()`, so anything the remote was holding stayed
      down on the host. Windows and macOS already released. It also made the applied counter
      consistent — Windows counted before dispatch, the other two after, and Linux skipped
      counting entirely for unknown event types.*

- [x] **1.7 Normalized-coordinate mapping is written three times.**
      `ScreenToVirtualDesk` (Windows), `SendMoveAbsolute` (Linux), `SourceRect` (macOS)
      all map a 0..65535 axis onto a rectangle with the same formula. One `core` helper.
      *Done — `core/input/PointerMap.h`: `ClampAbsCoord`, `AbsCoordToPixel` (integer),
      `AbsCoordToAxis` (floating point, for macOS's `CGPoint`) and the inverse
      `AxisToAbsCoord`. `kAbsCoordMax` moved here from `ClientInputQueue.h`, which now
      includes it, and `ClientInputQueue.cpp`'s private `ClampAbs` is gone. All three
      injectors use the shared helpers.*

- [x] **1.8 Wheel delta → notches.**
      `kWheelDelta = 120` and the notch rounding exist only on Linux
      (`client/linux/cpp/input/InputInjector.cpp`). Windows and macOS each handle the
      wheel differently. Normalise in `core`.
      *Done — `deskhub::WheelNotches()` and `kWheelDeltaPerNotch` in the same header. Linux
      and macOS both use it now (macOS still multiplies by 3 for its line-scroll unit).
      Windows passes the raw delta straight to `MOUSEEVENTF_WHEEL`, which is what the Windows
      API wants, so it does not call the helper.*

---

## Tier 2 — The two big loops

- [x] **2.1 `ClientLoop` exists five times.**

      | Platform | File | Lines |
      |---|---|---|
      | iOS | `client/ios/app/cpp/ClientLoop.cpp` | 282 |
      | Android | `client/android/app/src/main/cpp/ClientLoop.cpp` | 282 |
      | macOS | `client/macos/app/cpp/ClientLoop.cpp` | 280 |
      | Linux | `client/linux/cpp/ClientLoop.cpp` | 227 |
      | Windows | inlined in `client/windows/cpp/ClientApi.cpp:59-200` | ~200 |

      iOS and Android differ in **eight** places, all of them renames
      (`VtDecoder`→`MediaCodecDecoder`, `void* layer_`→`ANativeWindow* window_`,
      `SetLayer`→`SetWindow`). The headers differ in four lines.

      Two functions carry the duplication:
      - `NetThread()` (~100 lines, `ClientLoop.cpp:187-282`) — the
        `ClientPumpCallbacks` + `ClientNetLoopHooks` wiring is identical in all five.
        `deskhubp::RunClientNetLoop` already factors out the receive loop; the wiring
        around it was never factored out.
      - `DecodeThread()` (~80 lines) — queue handling, decoder rebuild and timing are
        identical; only the decoder type and the surface type change.

      *Done for iOS, Android, macOS and Linux.
      `platform/include/deskhubp/session/ClientEngine.h` is a `ClientEngine<Decoder, Surface>`
      template constrained on `VideoDecoderLike` + `RestartableDecoder` + the new
      `SurfaceBoundDecoder<D, Surface>` concept, with `RenderCountingDecoder` and
      `CongestionAwareDecoder` picked up through `if constexpr` so a decoder only pays for
      what it reports. All four `ClientLoop.cpp` files are deleted; each platform's
      `ClientLoop.h` is now a ~25-line subclass that keeps the names its bridge already
      calls (`SetLayer`/`SetWindow`, `Finished()`). 1071 lines of `.cpp` became one 400-line
      template. Android was rebuilt from clean and links.
      One change fell out on Linux: `AvDecoder` now forwards `TakeRenderedCount()` and
      `lastRenderedPtsUs()` from its `VideoSink`, which it already had. Before this, Linux
      never passed a rendered count to `ClientPump`, so the client's presented-frame stat
      was always zero there.*

      **Windows, done too.** `client/windows/cpp/decode/WinVideoDecoder.{h,cpp}` owns the
      `MfDecoder` + `PanelRenderer` pair behind the shared contract; its `Surface` is a
      `WinRenderTarget{device, renderer, negotiatedFps}`, which is also how the negotiated
      fps reaches `DecoderConfig` (the engine's `Init(Surface, w, h)` carries no fps).
      `ClientApi.cpp` went from 351 lines to 156 and is now only the C entry points plus the
      handle. Three things had to be added to the engine for it, all optional and all unused
      by the other four clients: `onDecodeThreadStart`/`onDecodeThreadExit` (Media Foundation
      needs `CoInitializeEx` on the decode thread), `onParams`/`onStatus`/`onEnded`/`onFinished`
      (the Win32 UI is driven by C callbacks, not by polling), and `statusSeparator` +
      `alwaysFocused`.

      Two behaviour changes on Windows, both deliberate:
      - A decoder that fails to initialise used to kill the session; now it retries, which is
        what the other four clients already did.
      - Input now goes through `deskhub::ClientInputQueue` like everywhere else, instead of a
        raw `std::vector<InputEvent>`, so held keys are tracked and released properly.

      Fixing this also caught a deadlock I had just introduced: `SetSurface()` waits for the
      decode thread to acknowledge the swap, which hangs if it is called before `Start()`.
      Linux did exactly that. The engine now only waits when the decode thread is running.*

- [x] **2.2 `AgentLoop` exists three times.**
      `client/linux/cpp/AgentLoop.cpp` (598), `client/macos/app/cpp/AgentLoop.cpp` (591),
      `client/windows/cpp/AgentLoop.cpp` (656). Linux↔macOS diff is 292 lines out of 1189.

      Four blocks are near-verbatim copies:

      | Block | Linux location | Note |
      |---|---|---|
      | `AttachSession` — `HostCallbacks` wiring | `AgentLoop.cpp:262-338` | `onHello`/`onStart`/`onNack`/`onInput`/`onFocus`/`onDisconnect`/`onFeedback` identical |
      | `RecvLoop` — beacon → `AcceptDatagram` → `Tick` → `RefreshOffer` → `DueForFlush` → 1 s stats | `AgentLoop.cpp:471-598` | differs only in log macro and encoder call |
      | `ShutdownPipeline` | `AgentLoop.cpp:341-361` | identical |
      | `PublishStatus` | `AgentLoop.cpp:363-397` | differs only in `zeroCopy` (see 1.5) |

      *Done — `platform/include/deskhubp/session/HostNetLoop.h` +
      `platform/src/session/HostNetLoop.cpp` (229 lines), following the `ClientNetLoop`
      shape. Two entry points: `MakeHostCallbacks()` builds the whole `HostCallbacks` set
      (negotiation, NACK, feedback, disconnect, and every log line that goes with them) from
      a small `HostSessionHooks`; `RunHostNetLoop()` owns the receive loop, the beacon reply,
      per-source `Tick`/`RefreshOffer`/`DueForFlush` and the one-second stats block.
      All three platforms now call it: 1845 lines across the three `AgentLoop.cpp` became
      1433 plus 229 shared, and the protocol sequence has one home instead of three.
      Windows compiles and `core_tests` passes; Linux and macOS are written blind.
      Two pieces of drift were normalised in the process: macOS only forced an IDR on a
      reconfig when the size had changed (now unconditional, matching Linux/Windows, since
      the client has to resync either way), and Windows never handled a source closing
      mid-session (now shared with Linux/macOS).*

---

## Tier 3 — Small, mechanical

- [x] **3.1 `dh_list_sources` is copy-pasted.**
      `client/ios/app/cpp/DeskhubClient.mm:25-45` and
      `client/macos/app/cpp/DeskhubBridge.mm:42-62` are line-for-line identical;
      `client/android/app/src/main/cpp/JniBridge.cpp:40-60` repeats the same logic in JNI
      form. Extract a `platform` helper.
      *Done — `platform/src/ffi/ClientFfi.cpp`, used by the iOS bridge, the macOS bridge and
      Android's `JniBridge` (which fills a `DHSourceInfo[deskhub::kMaxSources]` and then builds
      the Java strings from it, instead of repeating the parse/query/copy itself).*

- [x] **3.2 The C bridge headers overlap.**
      `DHPhase`, `DHSourceInfo`, `DHViewRect`, `DHViewTransform`, `dh_video_rect`,
      `dh_apply_gesture` and `dh_normalize_pointer` are declared identically in
      `client/ios/app/cpp/DeskhubClient.h` and `client/macos/app/cpp/DeskhubBridge.h`
      (iOS is a strict subset of macOS). One shared header.
      *Done — `platform/include/deskhubp/ffi/ClientFfi.h` holds the shared types and the
      three ViewFit shims, which are now compiled once in `platform/` instead of twice in the
      two `.mm` files. `DeskhubClient.mm` went 193 → 147 lines, `DeskhubBridge.mm` 300 → 254.
      The Windows `DeskhubApi.h` is a different API shape (handle-based, `__stdcall`) and does
      not share this header.*

---

## Tier 4 — Swift, deferred

- [ ] **4.1 Unify the iOS and macOS Swift layers.** — *needs a Mac; see the handoff section
      below for the suggested order and the `.pbxproj` caveat.*
      `StreamView` differs by 333 lines, `SessionModel` by 184, `ConnectView` by 210 —
      but the root cause is that the two bridges have different shapes: iOS uses global
      functions over a singleton (`dh_start` / `dh_stop`), macOS uses a handle
      (`dh_session_start(...) -> DHSession*`). Move iOS to the handle-based bridge first;
      after that `DeskhubClient.swift`, `SessionModel.swift` and `SourcePickerView.swift`
      can live in a shared `client/apple/swift/`. Large, needs a Mac. Do last.

---

## Where this stands

Everything except Tier 4 is done.

**Windows and Android are finished and verified by running them, not just compiling.**

- Clean rebuild from an empty `out/build/x64-debug`: 81/81 targets, no errors. `core_tests`
  passes standalone and through CTest. `make lint` (C++) is clean.
- Android: `app/.cxx` and `app/build` deleted, rebuilt from scratch, APK produced with native
  libs for both ABIs.
- The Windows app launches and its main window comes up.
- The host path was exercised end to end by running the agent headlessly
  (`Deskhub.exe --elevated-share --src m:<hmonitor>:<hexname> --fps 30 --bitrate 8`):
  `RunHostNetLoop` drove the per-second stats block, `ClampEncodeSize` produced 3440x1440 →
  1920x802, the Media Foundation encoder produced frames including the IDR, and
  `PublishStatus` published rows.
- The client path was exercised against that live agent with a throwaway driver that calls
  `dh_client_start_hwnd` on a real HWND: negotiation completed at 1920x802@30, `WinVideoDecoder`
  brought up MfDecoder + PanelRenderer over D3D11VA, frames decoded and presented, and the
  `present_ms` and `e2e_ms` diagnostics were populated — which is the `PresentTimingDecoder`
  path that only Windows uses. `dh_client_stop` joined cleanly with no stray "closed" callback,
  and the agent survived the client leaving.

That live run caught one more thing, now fixed: setting the surface before `Start()` left the
swap generation unacknowledged, so the decode thread treated it as a surface change on its
first iteration and fired a spurious `kf_req reason=dec_fail` at session start. `Start()` now
syncs the acknowledgement — there is nothing to tear down before the first decoder exists.

Not verified here — this machine has no toolchain for them, so these edits are written blind
and need a build on their own hosts: `client/linux/**`, `client/macos/**`, `client/ios/**`.
The Linux and macOS input injectors and `AgentLoop`s changed the most. Note that Linux sets
its `VideoSink` before `Start()`, exactly the path the two `SetSurface` bugs above were on.

What the round removed, in round numbers: the three `AgentLoop.cpp` went 1845 → 1427 lines
against 229 shared; the five `ClientLoop` implementations (1071 lines of `.cpp` plus 195
inline in `ClientApi.cpp`) became one 474-line template plus five ~25-line subclasses;
`ClientApi.cpp` went 351 → 156; the three input injectors lost their duplicated dispatch; the
two Apple bridges lost 92 lines of copy-pasted FFI.

Bugs found while merging, all fixed:

1. `FrameGate` — the Linux fps gate treated a stored timestamp of `0` as "no reference
   frame", so a source whose first frame landed on `t=0` was never gated.
2. Linux never reported presented frames to `ClientPump`, so the client's rendered-frame
   stat was always zero there.
3. Linux logged the local-user takeover but never released the keys the remote was holding,
   leaving them stuck down on the host.
4. `viewerAddr` survived a disconnect into the UI, because the row kept the last formatted
   peer address after the peer was dropped.
5. `SetSurface()` before `Start()` deadlocked, waiting for a decode thread that had not been
   spawned yet — and once that was fixed, still fired a spurious keyframe request.

---

## Handoff — picking this up on a Mac

Windows and Android are finished and were verified by running them. **macOS and iOS were
edited blind on a Windows machine and have never been compiled.** Everything below is what
you need to finish them; work through it in order.

### Build and verify

From the repo root on the Mac:

```sh
make test                 # core_tests, offline — start here, it needs no Xcode
make build-macos          # xcodebuild, Debug
make run-macos
make build-ios            # iphonesimulator, Debug
make run-ios
make lint                 # C++ / Kotlin / Swift, what CI enforces
```

`make test` should pass unchanged — it is the same `core_tests` that passes on Windows, and
it covers the new `FrameGate`, `ClampEncodeSize`, `PointerMap`, `InputApplier` and
status-projection logic. If it fails, the problem is in `core/`, not in the Apple code.

### Build-system questions already answered

These were checked against the two `.xcodeproj` files, so you do not need to re-investigate:

- **No `project.pbxproj` edits are needed.** Both projects are `objectVersion = 77` with
  `FileSystemSynchronizedRootGroup`, and neither ever referenced `ClientLoop.cpp` by name, so
  deleting it and adding nothing new under `client/<os>/app` is invisible to Xcode.
- **The new `platform/` sources are picked up automatically.** Both projects have a
  "Build libcore.a (CMake)" pre-build phase that runs `cmake --build ... --target platform`,
  and `platform/CMakeLists.txt` now lists `src/session/HostNetLoop.cpp` and
  `src/ffi/ClientFfi.cpp`.
- **The FFI symbols will link.** Both targets already pass `OTHER_LDFLAGS = "-lplatform -lcore"`,
  so `dh_list_sources`, `dh_video_rect`, `dh_apply_gesture` and `dh_normalize_pointer` — which
  moved out of the two `.mm` files into `platform/src/ffi/ClientFfi.cpp` — resolve from
  `libplatform.a`.
- **The new shared header is reachable.** `HEADER_SEARCH_PATHS` already contains
  `$(SRCROOT)/../../platform/include` on both targets, which covers both the Objective-C++
  compile and the Swift bridging-header import of `deskhubp/ffi/ClientFfi.h`.
- **C++20 is on.** Both targets are `CLANG_CXX_LANGUAGE_STANDARD = "gnu++20"`, which the
  `requires` clause on `ClientEngine` needs.

### What changed on the Apple side

| File | Change |
|---|---|
| `client/macos/app/cpp/ClientLoop.cpp`, `client/ios/app/cpp/ClientLoop.cpp` | **Deleted.** |
| `client/macos/app/cpp/ClientLoop.h`, `client/ios/app/cpp/ClientLoop.h` | Now a ~25-line subclass of `deskhubp::ClientEngine<VtDecoder, void*>` that keeps the old method names (`SetLayer`, `Finished`, `Start(server, sourceId, w, h)`), so `DeskhubBridge.mm` / `DeskhubClient.mm` call sites are unchanged. |
| `client/macos/app/cpp/AgentLoop.cpp` | `AttachSession` now builds its callbacks with `deskhubp::MakeHostCallbacks`; `RecvLoop` is now `deskhubp::RunHostNetLoop`; `PublishStatus` uses `deskhub::MakeSourceStatus` / `MakeSourceInfo`. 591 → 458 lines. |
| `client/macos/app/cpp/input/InputInjector.{h,mm}` | Derives from `deskhub::InputApplier<InputInjector, uint16_t>`; `Apply()` is now three lines that call `DispatchInput`. `SendKey` gained an ignored `scan` parameter, and `OnLocalUserTookOver` / `OnLocalUserIdle` replaced the inline log lines. Uses `AbsCoordToAxis` and `WheelNotches`. |
| `client/macos/app/cpp/input/MacKeyMap.cpp` | Raw VK literals replaced with the `kVk*` constants. Values are identical — worth a diff read, it is the one change with no compiler to catch a typo. |
| `client/macos/app/cpp/DeskhubBridge.{h,mm}`, `client/ios/app/cpp/DeskhubClient.{h,mm}` | Shared C types and the three ViewFit shims moved to `deskhubp/ffi/ClientFfi.h`; the duplicate implementations were deleted from both `.mm` files. |

### If something breaks, look here first

1. **`MacKeyMap.cpp`.** A mistranscribed constant compiles fine and shows up as one wrong key.
   Diff it against the previous revision before trusting it.
2. **`InputInjector::ReleaseAll` on macOS.** It iterates a copy of `held_.heldKeys()` and calls
   `SendKey(vk, 0, false)`, which writes back into `held_`. The copy makes that safe, but it is
   the kind of thing to confirm under a debugger once.
3. **`hooks.retarget` on macOS.** `MakeHostCallbacks` stores the client size into
   `st.cliW`/`st.cliH` and *then* calls `retarget()`, which reads them back and forwards to
   `capture.SetClientSize`. If negotiation comes out at the wrong size, this ordering is why.
4. **Linux-style `SetSurface` timing.** macOS and iOS call `SetLayer` *after* `Start()`, which
   is the safe order. Linux calls it before, which used to deadlock and then used to fire a
   spurious keyframe request; both are fixed in `ClientEngine`, but if you touch that ordering
   on Apple, that is the trap.

### Then: Tier 4.1, the Swift unification

This is the only backlog item still open, and it is the reason the two Swift layers still
diverge (`StreamView` by 333 lines, `SessionModel` by 184, `ConnectView` by 210). The root
cause is that the two bridges have different shapes:

- iOS: global functions over a process singleton — `dh_start(addr, id)`, `dh_stop()`,
  `dh_set_layer(layer)`, `dh_phase()`.
- macOS: a handle — `dh_session_start(addr, id) -> DHSession*`, `dh_session_stop(s)`, …

Suggested order:

1. Move iOS to the handle-based bridge. `DeskhubClient.mm` already wraps a
   `std::shared_ptr<ClientLoop>` in a file-scope global; the handle version is the same object
   returned to the caller instead. Keep the C names macOS already uses so the Swift side
   converges rather than gaining a third spelling.
2. Once the two bridges match, `DeskhubClient.swift`, `SessionModel.swift` and
   `SourcePickerView.swift` can move to a shared `client/apple/swift/`. **This step does need
   `.pbxproj` edits** — a new `FileSystemSynchronizedRootGroup` for `client/apple/swift` in
   both projects — because the synchronized groups only cover `client/<os>/app`.
3. `StreamView` and `ConnectView` are genuinely different (touch vs. pointer, phone vs.
   window). Do not force those together; share the model, not the view.

---

## Explicitly out of scope — these belong where they are

`capture/` (DXGI, ScreenCaptureKit, PipeWire/Portal), `encode/` (Media Foundation, NVENC,
VideoToolbox, VA-API), `decode/` (`MfDecoder`, `AvDecoder`, `MediaCodecDecoder`),
`render/` (`PanelRenderer`, `VideoRenderer`), the UI layers (`win32/`, `gtk/`, SwiftUI,
Compose), `Firewall.cpp`, `ElevatedShare.cpp`, `GpuSelect.cpp`, `Downscaler.cpp`,
`Permissions.mm`.

The three `capture/CaptureTypes.h` files are already minimal — each declares only its own
payload plus the shared `FrameMeta` and a `static_assert`. Leave them alone.
