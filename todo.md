# TODO — de-duplicate the client layer

Four parts, one per subsystem:

1. Session/agent loop + FFI
2. Input / keymap
3. Video encode / decode / render
4. UI / app-model

Within each part, items are ordered by value-per-risk (P0 first). Run `make test`
and `make lint` after each item. Items marked **BUG** are behavioural defects found
while comparing the copies, not just duplication.

Every unchecked item now opens with why it is still open. Everything left is **BLOCKED
on a toolchain**:

- The first pass ran on a macOS machine (macOS, iOS and Android built and tested
  there). A second pass on 2026-08-01 ran on a **Windows** machine: the Windows app
  builds clean, `make test` and `make lint` pass, and the previously-unbuilt Windows
  edits are verified. What no machine so far has had is **Linux** — anything touching
  VA-API, libav or the GTK viewer is stuck behind that. The cross-platform items also
  cannot be half-done from one OS:
  `HostSourceBase`, the `IVideoEncoder` move and the three `CaptureTypes.h` all change
  headers the unbuildable clients include, so a partial commit breaks them. Each item
  names the toolchain it wants.
- **3 of the 16 are also marked PARTLY DONE** — the reachable platforms are finished
  and the note says exactly which file still holds the old copy.
- Four of them (`HostSourceBase`, `PointerLockState`, the last-viewer-closed counter,
  `CapturedFrame<Handle>`) *could* technically be half-done from macOS alone, since
  the change would be additive. They were not, and the per-item notes say why: with
  only one of three backends compilable you would be designing the customization
  point blind, and for the counter the shared part is three lines of arithmetic while
  the action at zero differs on every platform.

The one deliberate skip — Android push-status callbacks under P4-P2 — is **done as of
2026-08-01**: the Windows machine can run the Android emulator, so the
`AttachCurrentThread` path was implemented and then verified live against the Windows
host (streaming, status updates, and the timeout-ended overlay all observed running).

Three Windows files (`MfEncoder.cpp`, `NvencEncoder.cpp`, `MfDecoder.cpp`) were edited
unbuilt for the `CodecName` cleanup. **Verified 2026-08-01 on a Windows machine**: all
three compile clean under MSVC (`make build-windows`) and `make test` passes — nothing
in the repo remains unbuilt. The same pass replaced the Windows downscaler's `& ~1u`
with `deskhub::EvenDown`, so the Windows half of the Part 3 math-helpers item is done
and that item now waits on Linux only.

# Part 1 — session/agent loop + FFI

Scope: `client/*/AgentLoop.cpp`, `client/*/ClientLoop.h`, the client-session FFI
backends, and the agent FFI. Baseline is already good: `deskhubp::HostEngine` and
`deskhubp::ClientEngine` own the loops. What is left is residual duplication inside
the policy tables plus two layers that were shared for the client but not the agent.

## P0 — biggest win, also fixes a real divergence

- [x] **`core`: add `AdmitCapturedFrame`.** New API next to
      `core/include/deskhub/session/HostRouter.h`:
      `struct FrameAdmission { bool drop; bool rebuildEncoder; bool paused; StreamSize encode; };`
      `FrameAdmission AdmitCapturedFrame(SourcePipelineState&, const media::FrameMeta&, uint32_t maxDim);`
      Absorbs: store `nativeW/nativeH`, `ClampEncodeSize`, encode-size-change detection,
      `sizeChanged`, and the `paused` flip-flop with its three log strings.
      Replaces `client/linux/cpp/AgentLoop.cpp:128-160`,
      `client/windows/cpp/AgentLoop.cpp:173-209`,
      `client/macos/app/cpp/AgentLoop.cpp:144-165`. (~25 LOC x 3)
- [x] **Tests** in `core/tests/control/StreamSizeTests.cpp` style: size change,
      too-small -> paused, grow-back -> resume, zero-size frame dropped.
- [x] **macOS: stop hand-rolling the sizing.** `client/macos/app/cpp/AgentLoop.cpp:139`
      uses `fi.meta.width & ~1u` and `:155` open-codes
      `kMinEncodeWidth`/`kMinEncodeHeight` instead of `deskhub::ClampEncodeSize`
      (`core/include/deskhub/control/StreamSize.h:35`). macOS also never writes
      `nativeW`/`nativeH`, so `RetargetStream` cannot work for it. Adopting
      `AdmitCapturedFrame` fixes both.
- [x] **macOS: unify the retarget path.** Decided: keep capture-side rescale, but there
      is now exactly one implementation of the formula —
      `deskhub::TargetStreamSize(src, maxDim, client, scalePct)` in
      `core/include/deskhub/control/StreamSize.h`, called by both `RetargetStream` and
      `ScreenCapture::ApplySizeLocked`. macOS cannot call `RetargetStream` itself:
      the capture backend rescales, so `st.nativeW/nativeH` hold the *already scaled*
      size and feeding it back through `FitStreamSize` would shrink the stream on every
      quality step. `ScreenCapture::Start` now also resets `cliW/cliH/qualityPct`, which
      had been leaking from the previous session. `Even` is gone in favour of
      `deskhub::EvenDown`.

## P1 — collapse the policy-table boilerplate

- [x] **`platform`: engine supplies the packet sink.** The `onPacket` lambda is
      byte-identical in all three files (`linux:98-102`, `macos:111-115`,
      `windows:137-141`) and only touches `HostSource` + `engine->socket()`.
      Add `HostEngine::MakePacketSink(HostSource&)` (or fill
      `HostSourcePolicy::packetSink` from inside `HostEngine::Start`). (5 LOC x 3)
- [x] **`platform`: default source policy.** These five hooks are character-identical
      across linux/macos/windows:
      | hook | linux | macos | windows |
      |---|---|---|---|
      | `status.closed` | 74-76 | 92-94 | 118-120 |
      | `source.releaseInput` | 198-200 | 199-201 | 269-271 |
      | `source.applyInput` | 202-204 | 203-205 | 273-275 |
      | `source.setEncoderBitrate` | 206-210 | 207-211 | 277-281 |
      | `source.inputSkipped` | 228-230 | 232-234 | 297-299 |
      Provide `template <class Pipeline> HostSourcePolicy MakeDefaultSourcePolicy()`
      in `platform/include/deskhubp/session/HostEngine.h`, constrained by a small
      concept (`.capture`, `.injector`, `.encoder`, `.encMutex`). Clients override
      only what is special. (~19 LOC x 3)
      Alternative to evaluate: make `SourcePipelineState` declare these as virtuals
      and drop the `std::function` indirection entirely.
- [ ] **BLOCKED: needs Linux + Windows.** macOS builds here, but all three `AgentLoop`s
      must move in one commit or the other two stop compiling.
      **`platform`: `HostSourceBase<Capture, Encoder>`.** Carries
      `capture` / `injector` / `encMutex` / `encoder`, the two `Pipeline()`
      down-casters (byte-identical: `linux:49-55`, `macos:59-61`, `windows:67-73`),
      and the cached-last-frame protocol. Collapses `linux:22-57`,
      `macos:25-61`, `windows:35-75`.
- [x] **`platform`: generic `source.create`.** `MakeSource<Pipeline>(engine, s, sourceId)`
      sets `sourceId` + `name`; the client assigns only its typed target handle.
      Replaces `linux:81-91`, `macos:96-104`, `windows:122-130`. (~10 LOC x 3)
- [x] **`core`: `MakeEncoderConfig(const SourcePipelineState&, StreamSize, uint32_t fallbackFps)`.**
      The `fps ?: optionFps` fallback plus the bitrate fill is triplicated business
      logic: `linux:106-112`, `macos:119-125`, `windows:146-155`. Clients then set
      only their extras (`srcWidth`/`srcHeight`, `outputPath`). (~18 LOC x 3)
- [x] **(done as `deskhubp::DiagEncode` in `HostEngine.h` — core cannot log)
      `core`: timed-encode helper on `SourceDiag`.** `DiagEncode` is the same
      algorithm and the same copy-pasted log literal in
      `linux:40-46`, `macos:52-58`, `windows:58-64`. `SourceDiag` already owns
      `encMs` (`core/include/deskhub/diag/AgentDiag.h:44`), so put it there.
      (7 LOC x 3)
- [ ] **BLOCKED: needs Linux + Windows** (rides on `HostSourceBase` above).
      **`platform`: one cached-last-frame mechanism.** Three answers to one
      requirement: macOS `cachedPb`/`ReleaseCached` (`macos:43-51,127,135-137,158,192-195`),
      Windows `cachedTex`/`haveCached` (`windows:55-56,194-195,225-239,303-308`),
      Linux pushes it into `VaEncoder::EncodeLast`/`haveSourceFrame`
      (`client/linux/cpp/encode/VaEncoder.h:34-38`). The *policy* is identical; only
      the retain/release primitive differs -> customization point on `HostSourceBase`.
- [ ] **BLOCKED: needs Linux + Windows**, and behaviour-sensitive — a keepalive that
      stops firing is invisible until a viewer times out, so this wants all three
      desktop builds running, not just compiling.
      **`core`: move `lastKeepaliveUs` bookkeeping next to `DueForFlush`**
      (`core/include/deskhub/session/HostRouter.h:47`). Currently assigned from three
      client files: `linux:239`, `macos:197`, `windows:309`.
- [x] **Fix the `LOGE`/`LOGW` inconsistency** on the identical capture-start failure:
      `linux:177` and `macos:179` use `LOGE`, `windows:250` uses `LOGW`.
- [x] **Promote the Windows port-error message to the default.**
      `client/windows/cpp/AgentLoop.cpp:92-98` is strictly more informative than
      `DefaultPortError` (`platform/src/session/HostEngine.cpp:13-16`).

## P2 — client session / FFI layer

- [x] **`platform`: `ClientEngineConfig::For(server, sourceId, screenW, screenH)`**
      (or a `ClientEngine::Start` overload). Both `client/linux/cpp/ClientLoop.h:11-21`
      and `client/android/app/src/main/cpp/ClientLoop.h:13-20` exist only to fill the
      same five fields. After this, linux's `ClientLoop.h` can disappear entirely and
      android keeps only `SetWindow`/`Finished`. (~9 LOC)
- [x] **(done as `deskhubp::FfiClientSession` in `ffi/ClientSessionShell.h`)
      `platform`: `DESKHUB_DEFINE_CLIENT_SESSION_SHELL` (or `deskhubp::FfiSessionBase`).**
      `ClientSessionForward.h:50,56` already *requires* `statusBuf`/`reasonBuf`, yet
      both backends re-declare the whole `DHSession` shell plus the identical
      `dh_session_start` prologue and `onParams`/`onStatus`/`onEnded` trampolines:
      `client/windows/cpp/ClientSessionWin.cpp:34-57,61-63,67-111,123-129` vs
      `platform/src/ffi/ClientSessionApple.mm:55-68,72-97,104-108`. (~55 LOC x 2)
      Windows keeps only D3D/renderer setup (`:80-88`), `negotiatedFps`, COM hooks
      (`:112-113`); Apple keeps only `LocalScreenPixels` (`:25-51`).
- [x] **BUG (found while comparing): Apple never wires `cfg.onFinished`.**
      `platform/src/ffi/ClientSessionApple.mm:95-97` sets only `onEnded`, so the
      socket-error / `"stopped"` path reported by
      `ClientEngine::NetThread` (`platform/include/deskhubp/session/ClientEngine.h:423-430`)
      is dropped on iOS/macOS. Windows handles both plus a `closedNotified` de-dup
      (`ClientSessionWin.cpp:52-56,108-111`). Fix as part of the shell extraction.
- [x] **Android: stop forking the forwarders.** New
      `client/android/app/src/main/cpp/ClientSessionAndroid.cpp` builds the `DHSession`
      shell out of `deskhubp::FfiClientSession` +
      `DESKHUB_DEFINE_CLIENT_SESSION_FORWARDERS`; `JniBridge.cpp` is now JNI -> C shims
      over the `dh_session_*` ABI and `ClientLoop.h` is deleted. `dh_session_key`,
      `dh_session_release_all_input` and `dh_session_mouse_wheel` are restored and
      exposed on `NativeClient` as `key`/`releaseAllInput`/`mouseWheel`. The one
      Android-local addition is `dh_session_set_screen_hint` in
      `ClientSessionAndroid.h`: Android takes the screen size from Kotlin, and
      `dh_session_start` has no parameter for it — kept client-local so the shared ABI
      (and the Windows backend, which cannot be built here) is untouched.

## P3 — agent FFI and the UI-side drive loop

- [x] **`platform`: `deskhubp/ffi/AgentSession.h` + `platform/src/ffi/AgentSession.cpp`.**
      No macro was needed after all: `AgentLoop` is already declared in `platform` (only
      `AgentLoop::Start` is per-client), so the whole `dha_*` surface is one ordinary
      translation unit, linked out of `libplatform.a` only by targets that reference it.
      `DeskhubBridge.mm` is down to `dh_map_key`, `dh_modifier_class` and the four
      `dh_has_*`/`dh_open_*` permission calls (160 -> 29 lines). `dha_last_error` is new,
      which the shared agent drive loop below will need.
- [x] **BUG: `DHAgentStatus` silently drops `zeroCopy`.**
      `client/macos/app/cpp/DeskhubBridge.h:27-38` has no field for it, so
      `dha_status` (`DeskhubBridge.mm:128-142`) never copies
      `AgentSourceStatus::zeroCopy` (`core/include/deskhub/media/AgentTypes.h:26`).
      Linux surfaces it in the UI (`client/linux/gtk/ShareWindow.cpp:190,194`);
      macOS cannot.
- [ ] **BLOCKED: needs Linux + Windows.** The macOS side is now ready for it —
      `dha_start`/`dha_status`/`dha_running`/`dha_last_error` all live in
      `platform/src/ffi/AgentSession.cpp`, so the loop has one C surface to drive.
      What is left is the two UI-side copies.
      **Unify the agent drive loop.** Same sequence in three places — start, show
      `LastError()` on failure, poll `Status()` on a 500 ms timer, tear down when
      `!running()`:
      `client/windows/win32/SessionWindow.cpp:48-56,182-201`,
      `client/linux/gtk/ShareWindow.cpp:134-166`,
      `client/macos/app/swift/DeskhubAgent.swift:65-86` + `AgentModel.swift:86-104`.
      (~40 LOC x 3)
- [x] **Expose the shared source labels through the FFI.** `DHSourceInfo` now carries
      `displayName` / `sizeLabel` / `pickerLabel` and `DHAgentStatus` carries `label`,
      all filled from `SourceLabel.h`. Swift's `Source` computed properties and
      `ShareView.label(for:)` are gone, and so are the per-call `dh_source_picker_label`
      / `dh_shared_source_label` buffer dances (both entry points deleted — nothing
      called them any more). Android picks the same fields up through the JNI struct
      path, which also settles the `x` vs `×` separator: `MainActivity` and
      `StreamActivity` used `×`, everyone else `x`; core's `x` wins.

## P4 — cosmetic / low priority

- [ ] **BLOCKED: needs Linux + Windows** — the three `CaptureTypes.h` must be verified
      together, and a `CapturedFrameLike` mismatch only shows up as a `static_assert`
      on the platform you did not build.
      **`core`: `CapturedFrame<Handle>` alias template** in
      `core/include/deskhub/media/CaptureContract.h`, so the three `CaptureTypes.h`
      shrink to one instantiation plus platform extras:
      `client/linux/cpp/capture/CaptureTypes.h:19-34`,
      `client/macos/app/cpp/capture/CaptureTypes.h:6-12`,
      `client/windows/cpp/capture/CaptureTypes.h:10-16`.
      All three repeat the same `static_assert(CapturedFrameLike<...>, ...)`.
- [ ] **BLOCKED: needs Linux + Windows.** Note the three injectors already agree on
      behaviour after the P0 fixes above (argument order, release-on-disable); what is
      left is purely that the shared members are declared three times.
      **Align the three `InputInjector` headers.** Everything except `Init`'s
      signature is already the same interface across
      `client/linux/cpp/input/InputInjector.h`,
      `client/macos/app/cpp/input/InputInjector.h`,
      `client/windows/cpp/input/InputInjector.h` (~25 declaration lines x 3).
      Consider a `deskhub::InputApplier`-side concept so the shared members are
      declared once and only `Init` stays per-OS.
- [ ] **BLOCKED: needs a Linux *run*, not a build** — this is a question about intent,
      and the only way to answer it is to watch what the GTK viewer logs.
      **Audit `ClientDiagCaps` per client** — linux passes the default `{}`
      (`client/linux/cpp/ClientLoop.h:7`) while android and apple pass `{false,true}`
      and windows `{true,false}`. Android's copy moved to
      `ClientSessionAndroid.cpp:29` when its `ClientLoop.h` was deleted, so the three
      non-linux clients are now easy to compare in one place. Confirm linux's `{}` is
      intentional and not an omission.

## Must stay per-client (do not touch)

Encoder/decoder/capture backends; `policy.preflight` bodies (D3D11 device
`windows:83-90`, portal permission `linux:67-72`, `macperm` `macos:77-83`);
`policy.afterSocket` firewall rule (`windows:100-109`); `policy.onSharing`
privilege warnings (`windows:111-116`, `macos:85-90`); the platform frame handle and
its retain/release; `InputInjector::Init` argument shape; all UI
(GTK / Win32 / SwiftUI / Compose).

## Rough impact

| Area | Duplicated LOC today | Est. after |
|---|---|---|
| `AgentLoop.cpp` x 3 (243 + 251 + 313 = 807) | ~430 shared | ~330 (~110/platform) |
| `ClientLoop.h` x 2 (51) | ~18 | 0-10 |
| `ClientSessionWin.cpp` + `ClientSessionApple.mm` (248) | ~110 | ~90 |
| `JniBridge.cpp` forwarders (60) | ~60 | ~15 |
| Agent FFI + drive loop | ~120 | ~50 |

# Part 2 — input / keymap

Scope: `client/*/input/`, the viewer input handlers, and the mobile touch layers.
Baseline: `core/include/deskhub/input/` already provides the protocol key space
(`VirtualKeys.h`), `ScancodeTable<Native>`, `KeyMap.h`, `PointerMap.h`, `Hotkeys.h`,
`InputApplier.h`, `PressedInputTracker.h`, `ClientInputQueue.h` — the problem is
inconsistent adoption, not absence.

## P0 — one scancode table, two injector bugs

- [x] **`core`: add `input/Set1Scancodes.h`** with
      `constexpr int32_t VkToSet1Scancode(int32_t vk)`. The vk -> set-1 scancode
      column is a PC/AT hardware constant, yet it is hand-written twice
      (~90-100 identical pairs): `client/linux/cpp/input/LinuxKeyMap.cpp:15-126`
      and `client/macos/app/cpp/input/MacKeyMap.cpp:14-88`. Both tables become
      two-column `native <-> vk`. Also lets
      `core/include/deskhub/input/Hotkeys.h:22-33` stop hardcoding
      `0x01/0x0F/0x1C/0x48|E0/...` (a third copy), and makes the Windows
      `MapVirtualKeyW` fallback (`client/windows/cpp/input/InputInjector.cpp:65`)
      unit-testable.
- [x] **BUG: Windows tracker stores `(scan, vk)` inverted.**
      `client/windows/cpp/input/InputInjector.cpp:60` calls
      `held_.SetKey(scan, vk, down)` — id/native swapped relative to linux
      (`InputInjector.cpp:157`) and macOS (`InputInjector.mm:171`). Compensated at
      `:130`, but `held_.FindKey(vk)` (the linux idiom at `.cpp:150`) would silently
      misbehave on Windows. Normalise the argument order.
- [x] **BUG: linux `SetEnabled(false)` never releases held keys.**
      `client/linux/cpp/input/InputInjector.h:27-29` is a bare flag write; Windows
      releases before (`InputInjector.cpp:45-49`), macOS after (`.mm:133-137`).
      Disabling injection mid-keypress leaves keys latched in the uinput device.
      Pick one semantic (release on disable) and apply it on all three.

## P1 — shared cursor/pointer math

- [x] **`core`: `input/TrackpadCursor.h`** — `TrackpadCursor{x, y}` plus
      `ClampToVisible`, `MoveCursorBy`, `CursorScreenPoint`, `NormalizeCursor`, tested
      in `core/tests/input/TrackpadCursorTests.cpp` and exposed as `dh_cursor_*`.
      iOS goes through `client/apple/swift/TrackpadCursor.swift`, Android through
      `NativeClient.cursor*`; both hand-rolled algorithms are gone.
- [x] **BUG-ish: mobile normalization divided by `extent`, core by `extent - 1`.**
      Settled in favour of core: `NormalizeCursor` emits through `NormalizeAxis`, so a
      trackpad cursor and a direct tap on the same spot now produce the identical
      coordinate (asserted in the tests). The literal `65535` is gone from both
      mobile clients.
- [ ] **BLOCKED: needs a Linux host to test against** — both readings compile, the
      difference is one pixel at the far edge of the desktop. **Reconcile
      `AxisToAbsCoord` / `AbsCoordToPixel` with `NormalizeAxis`.** The constant merge is
      done (`kNormalizedMax` aliases `kAbsCoordMax`) and the client-emission side is
      settled by `TrackpadCursor`. What is left is that `PointerMap.h:24-30` treats its
      `extent` as a span while `NormalizeAxis` treats it as a pixel count: Windows
      compensates by passing `w - 1` (`win InputInjector.cpp:22-23,94-95`), Linux passes
      the raw extent (`linux InputInjector.cpp:179-183`). One of the two is wrong by a
      pixel; deciding it needs a Linux host to test against.
- [x] **`core`: `ModifierClass(int32_t vk)`** in `VirtualKeys.h`. Collapses
      `mackeys::ModifierOf` (`MacKeyMap.cpp:101-123`), `PreferLeftModifier`
      (`ScancodeTable.h:10-17`), and — via FFI — the parallel Swift table at
      `client/macos/app/swift/RemoteView.swift:127-136` (hardcodes the same mac
      keycode pairs as `MacKeyMap.cpp:46-53`; drifts silently if the C++ table
      changes).
- [ ] **BLOCKED: needs Linux + Windows** — all three halves must land together or the
      hint strings drift again. Groundwork is already in place: `ViewerTitle.h` owns
      `kViewerLockHint`/`kViewerLockedHint` and `ViewerStatusWithHint`, reachable from
      Swift via `dh_viewer_subtitle`, so only the *state machine* is still triplicated.
      **`core`: `PointerLockState`** — `{locked, paused}` + `OnToggleKey()` /
      `OnFocusLost()` / `HintText()`. Three unrelated ~20-LOC state machines:
      `client/windows/win32/ViewerInput.cpp:71-89,115-118,177-179`,
      `client/linux/gtk/ViewerWindow.cpp:229-268,353-358`,
      `client/macos/app/swift/RemoteView.swift:86-105,179-183`. Also unifies the F9
      constant, still written three ways: `ViewerInput.cpp:17`, `ViewerWindow.cpp:18`,
      and the bare `0x65` at `RemoteView.swift:94,103`.

## P2 — FFI surface and small remaps

- [ ] **BLOCKED: needs Linux.** Doing the macOS half alone would just rename a working
      entry point and leave a second one unused, so wait for the GTK side.
      **`platform`: promote `dh_map_key`** (macOS-only,
      `client/macos/app/cpp/DeskhubBridge.h:14`, `.mm:6-9`) into
      `deskhubp/ffi/ClientFfi.h` as a per-OS `dh_native_key_to_vk(native, *vk, *scan)`.
      The GTK viewer then uses the same entry point instead of calling
      `linuxkeys::EvdevToWin` directly (`ViewerWindow.cpp:270-273`).
- [x] **One FFI marshalling style.** JNI now goes through `dh_list_sources` /
      `dh_hotkeys` and builds `NativeClient.Source` / `NativeClient.Hotkey` instances
      directly, so the tab-separated rows and the Kotlin re-parsers are gone.
      `deskhub::kMaxHotkeys` replaces the hand-counted buffer size.
- [x] **`core`: `DispatchHotkey(queue, hotkey)`** in `Hotkeys.h`, tested against a
      recording queue, and exposed as `dh_session_hotkey` from
      `DESKHUB_DEFINE_CLIENT_SESSION_FORWARDERS` (so Windows picks it up for free).
      iOS calls `model.hotkey(hotkey)`, Android `NativeClient.hotkey(hk)`; both
      `modVk != 0 ? keyChord : keyTap` copies are gone.
- [x] **Use `kWheelDeltaPerNotch` instead of the literal 120.** New
      `dh_session_mouse_wheel_notches` in the forwarder macro applies the constant in
      core; macOS `RemoteView.scrollWheel` now sends notches, and Android's
      `NativeClient.mouseWheel` takes notches too. The GTK literal was already gone.
- [x] **Mobile wheel gesture.** Two-finger drag now scrolls the remote wheel *while the
      video is unzoomed*, and keeps panning the video once zoomed. Nothing was taken
      away: at zoom 1 `ApplyGesture` clamps the pan to a no-op, so that gesture was
      already doing nothing. Pinch is untouched on both — iOS routes only the pan
      recognizer, Android only reroutes when `factor == 1f`.
      The accumulation rule is shared, not copied: `TakeScrollNotches(dragPoints, carry)`
      + `kTouchPointsPerNotch` in `input/PointerMap.h`, tested in
      `core/tests/input/PointerMapTests.cpp`, reached through `dh_take_scroll_notches`.
      Both clients carry the sub-notch remainder across gestures so a repeated flick
      never loses scroll, and both drop it when the gesture turns into a zoom or pan.
- [ ] **PARTLY WON'T-DO, remainder BLOCKED: needs Linux.**
      **`core`: viewer-side button remap tables.** The win32 half is a WON'T-DO:
      `WM_*BUTTON*` / `GET_XBUTTON_WPARAM` (`ViewerInput.cpp:24-27,153-164`) are
      windows.h symbols and `core/` may not include OS headers. The GDK half
      (`ViewerWindow.cpp:318-326`) is plain ints and *is* liftable — it just needs
      Linux. Related, and deliberately left alone: the `MouseButton` enum
      (`protocol/Wire.h:140-144`) is mirrored truncated in
      `client/apple/swift/ClientSession.swift:10-14` (3 of 5) and
      `NativeClient.kt:83-84` (2 of 5). Not a defect — neither mobile client has any
      way to produce X1/X2 — but if the enum ever grows, those two mirrors are where
      it will silently disagree.

## Must stay per-client (do not touch)

uinput device creation/ioctls (`linux InputInjector.cpp:22-82`); CGEvent synthesis
and double-click state (`mac InputInjector.mm:139-152,219-249`); `SendInput`
scancode/extended flags (`win InputInjector.cpp:59-74`); RAWINPUT registration and
decode (`ViewerInput.cpp:31-65,100-132`); gesture recognizers
(`TouchInputView.swift:59-90`, `StreamActivity.kt:405-422`, GDK event masks);
IME/text-entry adapters (`KeyInputView.kt`, `KeyInputView.swift`); pointer
grab/hide syscalls and accessibility checks.

# Part 3 — video encode / decode / render

Scope: `client/*/encode/`, `client/*/decode/`, `client/*/render/`, `client/*/capture/`
math. Baseline: `VideoContract.h` concepts are enforced everywhere
(every backend `static_assert`s) — the duplication is in the implementations under
an already-unified shape.

## P0 — one NAL parser, one letterbox

- [x] **`core`: add `media/AnnexB.h`** — `NalRef{span, type}`, `ParseAnnexB`,
      `NextStartCode`, `FirstVclOffset`, `ContainsIdr(span, Codec)`,
      `SplitParameterSets`, `AnnexBToAvcc`, `AvccToAnnexB`. Today there are five
      independent scanners:
      `client/windows/cpp/encode/MfEncoder.cpp:444-458`,
      `client/android/.../decode/MediaCodecDecoder.cpp:13-31`,
      `platform/src/media/VtDecoderApple.mm:20-44`,
      `client/macos/.../encode/VtEncoder.mm:100-109`, and the correct one hidden in
      an anonymous namespace in `core/src/media/H264Sps.cpp:168-244` (rebuild it on
      top of the new header). Deletes ~120 LOC.
- [x] **BUG (fixed by the above): `ContainsIdrNal` misses tail 4-byte start codes**
      (`MfEncoder.cpp:452` — `i+4 < len` should be `i+3 < len`) and is H.264-only
      while `SubtypeFor` (`:98-100`) advertises HEVC — **HEVC keyframes are never
      detected**, so SPS/PPS is never prepended and every packet reports
      `keyframe=false`. `MediaCodecDecoder.cpp:13-31` has the same off-by-one family.
- [x] **BUG: linux GL renderer hand-rolls the letterbox.**
      `client/linux/cpp/render/VideoRenderer.cpp:352-357` computes its own aspect
      fit while the input mapping uses `deskhub::FitVideoRect`
      (`ViewerWindow.cpp:132`) — the two can drift, i.e. latent pointer
      misalignment. Smallest change in this part: call `FitVideoRect`.

## P1 — rate control and presets become real

- [x] **(core + tests + MfEncoder/NvencEncoder/VtEncoder done; adopting it in
      `VaEncoder.cpp:614-615` still needs Linux — it is the `bps/2` outlier)
      `core`: `media/RatePlan.h`** —
      `PlanRateControl(bps, fps, rc, lowLatency) -> {frameBits, vbvBytes, vbvInitialBytes}`.
      The same formula appears four times with one outlier:
      `MfEncoder.cpp:306-308`, `NvencEncoder.cpp:115-116,174-175,191-192`,
      `VtEncoder.mm:198-206`, and `VaEncoder.cpp:614-615` which uses
      **`bps/2` (half a second) instead of one frame** — inconsistent with everyone
      else; settling it makes the outlier visible.
- [x] **(MF + NVENC + VT now honour both fields — VT maps `lowLatency` onto
      `RealTime` / `AllowFrameReordering` / `MaxFrameDelayCount` and skips
      `DataRateLimits` entirely under VBR; VaEncoder still needs Linux)
      Wire `EncoderConfig::rc` and `lowLatency` for real.** Declared at
      `core/include/deskhub/media/VideoTypes.h:25-26`, blessed by
      `ContractTests.cpp:192-193`, **read by zero backends** — every encoder
      hardcodes its own dialect of infinite-GOP / no-B / CBR / low-latency
      (`MfEncoder.cpp:300-305`, `NvencEncoder.cpp:102-121`,
      `VtEncoder.mm:167-174`, `VaEncoder.cpp:240-266,559-560`). Add a
      `deskhub::media::EncoderPreset` and make backends consume it.
- [x] **(WinVideoDecoder, MediaCodecDecoder and VtDecoder all use it now; only the
      linux `VideoRenderer.h:28-33` copy is left and needs Linux)
      `core`: `media::PresentCounters` value type** (`Add`, `TakeCount`,
      `lastPtsUs`, `TakePresentDelayMs`). Hand-rolled four times:
      `WinVideoDecoder.cpp:57-67`, `MediaCodecDecoder.h:27-31` + `.cpp:160-164`,
      `VtDecoder.h:25-33` + `VtDecoderApple.mm:202-206` (byte-identical to
      MediaCodec's), `VideoRenderer.h:28-33`. `ClientEngine::HarvestDecoder`
      (`ClientEngine.h:249-272`) already consumes them uniformly.
- [ ] **BLOCKED: needs Linux.** The macOS half is *done*: `Even()` is
      now `deskhub::EvenDown` and the mac copies in `ScreenCapture.mm` and
      `AgentLoop.cpp` are gone. The Windows half is *done* too (2026-08-01):
      `Downscaler.cpp` now calls `deskhub::EvenDown`, built and tested on Windows.
      Everything still outstanding lives in VA-API. Deliberately not written ahead of
      time — `AlignEncodeSize` and `LevelFor` are easy to unit-test but would sit in
      `core/` unused until a Linux machine can adopt them, and unused API is what this
      file exists to remove.
      **`core`: expose the small math helpers.** `& ~1u` still appears at
      `linux ScreenCapture.cpp:220-221`. Add
      `AlignEncodeSize(w, h, align)` for VA's 16-px macroblock alignment
      (`VaEncoder.cpp:203-206`; crop-offset expression duplicated at `:109-111` and
      `:572-574`), `H264Level.h::LevelFor` (`VaEncoder.cpp:41-65`, pure spec table),
      and consider lifting the CQP controller (`VaEncoder.cpp:350-373,773-796`) next
      to `BitrateController.h`.

## P2 — name the interfaces that already exist

- [ ] **BLOCKED: needs Linux + Windows** — this moves headers the Windows and Linux
      backends include, so it cannot be split. `IsOpen()` on the encoder is already done.
      **Move `IVideoEncoder`/`IVideoDecoder` to
      `platform/include/deskhubp/media/`** (precedent: `VtDecoder.h` is already
      there, shared by macOS + iOS). **Not core** — `Init` takes `ID3D11Device*`.
      Factory files per OS mirroring the `DisplayEnum{Win,Linux,Mac,None}.cpp`
      layout. While moving:
      - add `IsOpen()` to the encoder interface — `win AgentLoop.cpp:145` can only
        test the pointer, linux/mac test `IsOpen()`; Windows can keep a half-dead
        encoder alive.
      - rename `VaEncoder::Finish()` -> `Close()` — on Windows/macOS `Finish` means
        "drain and stop" (`MfEncoder.cpp:676-702`, `VtEncoder.mm:261-268`); on
        linux it means "destroy everything" and is called from `Init` as a reset
        (`VaEncoder.cpp:190,311-348`). Same name, opposite lifetime semantics.
      - add a named concept bundle
        `EngineDecoder = VideoDecoderLike && RestartableDecoder && SurfaceBoundDecoder<S>`
        replacing the `requires` block repeated at `ClientEngine.h:54-57` and five
        `static_assert` sites.
- [x] **(NVENC now refuses a crop mismatch and falls back to MF; the false test
      comment is fixed)
      BUG: `EncoderConfig::srcWidth/srcHeight` contract is false.**
      `ContractTests.cpp:195-196` claims "every platform reads it that way"; only
      `MfEncoder.cpp:344` reads it, and NVENC registers the input texture at
      `width/height` (`NvencEncoder.cpp:211-212`) — a genuinely different
      `srcWidth` would corrupt NVENC output. Fix the contract or the backends.
- [ ] **PARTLY DONE — VT done; the VA + libav guards are BLOCKED on Linux.**
      **`EncoderConfig::codec` was silently ignored.** Settled as "refuse what you cannot emit": `VtEncoder::Init` now fails
      with a named codec instead of quietly producing H.264, and the triplicated
      `codec == HEVC ? "HEVC" : "H264"` ternary is now `deskhub::media::CodecName`
      (adopted in MF/NVENC/MfDecoder — compiled clean on Windows 2026-08-01).
      `VaEncoder.cpp:240` still needs the same guard.
      Not a defect after all: the *decoders* never receive a codec —
      `Init(surface, w, h)` is the whole contract — so `MediaCodecDecoder.cpp:11`
      hardcoding `video/avc` is the only thing it can do. Plumbing a codec into the
      decoder contract is only worth it once HEVC is negotiable, and it is not:
      `ClientPump.cpp:53` always sends `kCodecMaskH264` and `HostSession.cpp:20`
      rejects anything else, which is why MF/NVENC's HEVC support is dead code today.

## P3 — Windows-local cleanups

- [x] **(done as `client/windows/cpp/gpu/D3D11VideoProcessor.{h,cpp}` — only the Windows
      client uses it, so `platform/` would break the one-API-everywhere rule)
      `platform`: `D3D11VideoProcessor` wrapper.** The same ~60-line
      enumerator/processor/output-view/colour-space sequence plus the
      `std::map<texture, InputView>` cache is triplicated:
      `MfEncoder.cpp:340-425`, `PanelRenderer.cpp:102-177`,
      `Downscaler.cpp:34-145`. (~150 LOC)
- [x] **(done as `client/windows/cpp/gpu/HrCheck.h`; the per-file macros are now
      one-line aliases so call sites stayed put)
      `platform`: `diag/HrCheck.h`** — one tagged HRESULT macro replacing
      `MF_CHECK`/`MF_CHECKI` (`MfEncoder.cpp:28-46`), `MFD_CHECK`
      (`MfDecoder.cpp:24-32`), `PR_CHECK` (`PanelRenderer.cpp:21-29`), `DS_CHECK`
      (`Downscaler.cpp:10-19`).
- [x] **(moved to `client/windows/cpp/gpu/`; `GpuVendorName` now returns `const char*`)
      Move `GpuSelect` out of `client/windows/cpp/capture/`** — used by the agent
      (`AgentLoop.cpp:84`) *and* the viewer (`ClientSessionWin.cpp:80`); "capture"
      is the wrong home. Also `GpuVendorName` returns `const wchar_t*`
      (`GpuSelect.h:16`) while the contract standard is `const char*`.
- [x] **(one `OpenEncoderOutput` helper in `IVideoEncoder.h`; `outputPath` now defaults
      to empty so file capture is opt-in)
      Deduplicate or delete the file-output plumbing.** `MfEncoder.cpp:248-261`
      and `NvencEncoder.cpp:149-161` are the same block; `outputPath` is the only
      reason the derived `EncoderConfig` exists (`IVideoEncoder.h:17-19`) and the
      agent always clears it (`win AgentLoop.cpp:154`).

## Must stay per-client (do not touch)

MFT event pump (`MfEncoder.cpp:102-143,500-702`, `MfDecoder.cpp:61-253`); NVENC
session/resource lifecycle (`NvencEncoder.cpp:54-146,203-329`); VA-API
config/context/buffer lifecycle and the hand-written SPS/PPS bitstream
(`VaEncoder.cpp:106-187,235-348,531-808`); VideoToolbox session + CoreMedia
callback (`VtEncoder.mm:52-232`); MediaCodec buffer dance
(`MediaCodecDecoder.cpp:85-158`); `AVSampleBufferDisplayLayer` enqueue
(`VtDecoderApple.mm:138-199`); libavcodec + hwdevice (`AvDecoder.cpp:29-145`);
all presentation (`PanelRenderer.cpp`, `VideoRenderer.cpp`,
`VideoLayerView.swift`); dma-buf import/export (linux-local helper at most);
the `ScreenCapture` implementations (WGC / ScreenCaptureKit / PipeWire).

# Part 4 — UI / app-model

Scope: SwiftUI (ios/macos), Compose (android), GTK, win32, and the shared
`client/apple/swift/` tree. Note: `client/apple/swift/` is a
`PBXFileSystemSynchronizedRootGroup` in **both** Xcode projects — any file dropped
there compiles into both targets with zero project edits (must build on both SDKs).

## P0 — free wins and one real bug

- [x] **BUG: win32 queries sources on the UI thread.**
      `client/windows/win32/MainMenuWindow.cpp:164` calls `QuerySources`
      synchronously inside the `WM_COMMAND` handler — the UI freezes for the whole
      query timeout. GTK uses a worker thread (`MainWindow.cpp:352`), Swift
      `Task.detached`, Kotlin `Dispatchers.IO` (`NativeClient.kt:222`).
- [x] **Move `ViewTransform.swift` and `Hotkeys.swift`** from
      `client/ios/app/swift/` into `client/apple/swift/` — both are pure FFI
      wrappers over `ClientFfi.h` symbols available to both SDKs. Then rewrite
      `client/macos/app/swift/RemoteView.swift:58-70` (`videoRect`) to call
      `dh_video_rect` instead of hand-rolling `FitVideoRect` in Swift.

## P1 — shared strings, labels, connect flow

- [x] **(adopted in GTK + win32 + macOS Swift, and now iOS + Android too: "Session
      ended" is `DHStrSessionEnded` and "Connecting to ..." is `dh_connecting_to`,
      reached from Kotlin through the new `nativeString` bridge)
      `core`: `ui/Strings.h` + `dh_string(id)` in `ClientFfi.h`.** ~16 UI strings
      appear verbatim in 2-4 clients (e.g. "Others connect to you using one of
      these IP addresses:" at `MainWindow.cpp:178`, `MainMenuWindow.cpp:257`,
      `ConnectView.swift:52`; "Could not open a viewing session..." at
      `MainWindow.cpp:381`, `Viewer.cpp:243`, `StreamView.swift:43`; full list in
      the review). Also fixes the port literal: GTK interpolates `kDeskhubPort`
      (`MainWindow.cpp:206-208`) while win32 (`MainMenuWindow.cpp:157,280`) and
      macOS (`ConnectView.swift:73`) hardcode `47777`.
- [x] **Expose `SourceLabel.h` through the FFI**
      (`dh_source_picker_label`, `dh_shared_source_label`). Five re-implementations:
      `client/apple/swift/ClientSession.swift:22-24`,
      `client/ios/app/swift/StreamView.swift:239-243`,
      `client/macos/app/swift/ShareView.swift:36-39` (a character-for-character
      Swift transliteration of `SharedSourceLabel`, `SourceLabel.h:22-30`),
      `MainActivity.kt:239-244`, `StreamActivity.kt:456-459` — with an `x` vs `×`
      separator inconsistency between them. (Overlaps Part 1 P3 last item — one
      change serves both.)
- [x] **(rule + tests in `session/ConnectFlow.h`; desktop pickers adopted, mobile already
      matched — full state machine not needed)
      `core`: `ConnectFlow` state machine + FFI.** The rule "query sources; >1 ->
      picker; exactly 1 -> auto-start; 0/failed -> start with sourceId 0" is
      hand-written five times and has drifted:
      `client/ios/app/swift/SessionModel.swift:20-38`,
      `client/macos/app/swift/ConnectView.swift:163-191`,
      `MainActivity.kt:113-128`, `MainWindow.cpp:330-386` (+`:59-66`),
      `MainMenuWindow.cpp:140-172` (+`:70-74`). Unit-testable in `core/tests/`,
      which none of the five copies are; forces the P0 win32 threading bug out.
- [x] **`platform`: `dh_parse_address()` in `ClientFfi.h`.** GTK/win32 validate
      up-front with a dedicated message (`MainWindow.cpp:340-348`,
      `MainMenuWindow.cpp:153-160`); iOS/macOS/Android fall through to a generic
      late "Could not connect" (`ClientFfi.cpp:18-22`, `JniBridge.cpp:81-85`,
      `SessionModel.swift:53`, `StreamActivity.kt:346`). Three lines of glue over
      the existing `ParseNetAddr` (`deskhubp/net/UdpSocket.h:22`).

## P2 — poll loops, apple divergences, small state machines

- [ ] **PARTLY DONE — Apple and Android done; only the GTK viewer still polls,
      BLOCKED on the Linux toolchain.**
      **Retire the 500 ms status-poll loops in favour of `DHSessionCallbacks`.** `StreamModel` no longer runs a `Timer`:
      `ClientSession.start` takes a `SessionHandlers` value, boxes it with
      `Unmanaged.passRetained` for the `void* user` slot, and releases it in `stop()`
      (safe because `dh_session_stop` joins the engine threads before returning).
      `resumePolling`/`suspendPolling` collapsed into one `refresh()` for `onAppear`.
      Android done 2026-08-01: `JNI_OnLoad` caches the `NativeClient` class and three
      static method ids, the callback trampolines attach/detach the engine's net
      thread per call, `NativeClient` posts to the main looper, and `StreamScreen`
      installs a `SessionListener` in a `DisposableEffect` seeded with one initial
      read. Trampolines read the session through an atomic set only after
      `dh_session_start` returns and cleared on stop; join-before-delete makes that
      safe. One runtime-only trap, exactly the kind the old skip note feared: Kotlin
      mangles `internal` JVM names (`onSessionStatus$app_debug`), so the methods carry
      `@JvmName` — without it `GetStaticMethodID` aborts the VM at `loadLibrary`.
      Verified live on an emulator against the Windows host: status line ticking,
      `1920x802` size from `onSize`, and the network-loss "Session ended (timeout)"
      overlay from `onClosed`.
      Still polling: `ViewerWindow.cpp:115,183-192`.
- [x] **Shared Apple connect model.** `client/apple/swift/ConnectModel.swift` owns
      `address` / `isConnecting` / `connectError`, the `lastAddress` `UserDefaults`
      round-trip, and the accept-address step; both `SessionModel`s hold one and the
      views bind through `model.connect.*`. It is a held `@Observable` rather than a
      base class, because a subclass of an `@Observable` class cannot add observed
      properties of its own and iOS's model needs `screen`/`sources`/`stream`.
      The invalid-address message now comes from `DHStrInvalidAddressHint`, the same
      text GTK and win32 show.
- [x] **`core`: viewer-title helpers + constants.** Title helpers were already done
      (`ViewerBaseTitle`, `kViewerLockedHint`, `ViewerStatusWithHint`, FFI
      `dh_viewer_base_title`/`dh_viewer_subtitle`). `kZoomedThreshold` now lives in
      `ViewFit.h` with an `IsZoomed(zoom)` helper and a `dh_is_zoomed` entry point;
      `kViewerMarginPx` was already there and both desktop viewers already use it.
- [x] **Drive the macOS quality picker from `kQualityPresets`.** New
      `dha_quality_presets` in the agent FFI feeds `DeskhubAgent.qualityPresets`, and
      `ConnectView` builds the picker from it instead of four hardcoded tags.
      The `max(640, maxDim)` floor is gone: every rung the picker can produce is
      1280 or larger (or 0 for Native), so the clamp could never bind — it was dead
      code that only made macOS look different from the other two.
- [ ] **BLOCKED: needs Linux + Windows** — "last viewer closed" decides whether the app
      quits, so a half-migration that miscounts on one platform strands a process.
      **`core`: last-viewer-closed counter.** Same invariant in three languages:
      `macos App.swift:4-7` + `StreamView.swift:31-38`,
      `MainWindow.cpp:370-373` (`openViewers_`), `Viewer.cpp:26,139`
      (`g_openFrames`). Small, but it is a state machine, not view code.

## P3 — mobile HUD rules

- [x] **Extract the shared iOS/Android HUD rules.** Everything with a *value* in it now
      comes from core through the FFI: `dh_string(DHStrSessionEnded)`,
      `dh_connecting_to`, `dh_host_title` (which also settles `—`/`×` spelling),
      `dh_zoom_label`, and `dh_is_zoomed` over the new `kZoomedThreshold` — Swift's
      `ViewTransform.isZoomed` and Kotlin's `zoom > 1.01f` were the same constant twice.
      Android gained a `nativeString` bridge on the way, so the rest of `ui/Strings.h`
      is now reachable from Kotlin too.
      Deliberately *not* extracted: pan-mode-follows-zoom, reset-on-source-switch and
      "Display only when >1 source" are one-line reactions in two different reactive
      frameworks (`onChange` vs `LaunchedEffect`). There is no value to share, only
      shape, and an FFI round-trip would be more code than the rule.

## Must stay per-client (do not touch)

All SwiftUI/Compose/GTK/win32 layout and widget code; NSEvent/RAWINPUT/GDK event
translation; video surface plumbing (`VideoLayerView.swift`, `GtkGLArea`,
`SurfaceHolder`, child video HWND); macOS permissions
(`AgentModel.refreshPermissions`, `ConnectView.swift:38-47,141-153`); Windows
elevation/firewall (`ElevatedShare.cpp`, `MainMenuWindow.cpp:110-132`); GTK
portal/monitor geometry; Android lifecycle/Intent marshalling; the
one-window-per-source vs one-screen topology difference.
