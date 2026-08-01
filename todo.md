# TODO — de-duplicate the client layer

Four parts, one per subsystem:

1. Session/agent loop + FFI
2. Input / keymap
3. Video encode / decode / render
4. UI / app-model

Within each part, items are ordered by value-per-risk (P0 first). Run `make test`
and `make lint` after each item. Items marked **BUG** are behavioural defects found
while comparing the copies, not just duplication.

**Every item in this file is now closed** — done, or resolved as a won't-do with the
reason kept in place so it is not re-proposed. The won't-dos are: the
`IVideoEncoder`/`IVideoDecoder` move (Part 3 P2), the win32 button-remap table
(Part 2 P2), and the per-OS pieces each item names — toggle-key constants, `Init`
signatures, the pointer-grab syscalls.

A fourth pass on **2026-08-01** finally ran on **Linux** — the machine that every
remaining item was waiting for. GTK 3, PipeWire, EGL/libdrm and VA-API are all present,
`third_party/ffmpeg-min` was already built, and `make build-linux`, `make test` and
`make lint` all pass. The VA-API encoder was exercised against real hardware (Intel iHD
on `/dev/dri/renderD128`, `EncSliceLP`): CBR and VBR, low latency on and off, a bitrate
change mid-stream, the keepalive re-encode of the cached frame, and an
encode → libavcodec-decode round trip that confirms a non-macroblock size (1918x1078)
comes back out at exactly 1918x1078, so the new SPS crop offsets are right.

What that pass could **not** exercise is the interactive part: sharing goes through the
XDG desktop portal, which needs a human to approve the picker, so the live
agent → viewer session was not driven end to end on this machine. Nothing below depends
on that — the pieces it would cover (capture, portal, GTK dialogs) are all in the
"must stay per-client" list.

Earlier passes, for context: the first ran on macOS (macOS, iOS and Android built and
tested there); a second on Windows verified the previously-unbuilt Windows edits
(`MfEncoder.cpp`, `NvencEncoder.cpp`, `MfDecoder.cpp` compile clean under MSVC) and
landed the Windows `EvenDown` adoption plus the Android push-status callbacks; a third
went back to macOS for the Apple halves.

## What still needs a Windows and a macOS build

The additive-only rule that governed earlier passes has been retired: with every item
closed, the last three half-landings (`HostSourceBase`, the cached-last-frame slot,
`CapturedFrame<Handle>`) were finished on Windows too, so nothing is deliberately
half-applied any more. The cost is that **the Windows and Apple edits from this pass
have not been compiled**. They are all mechanical and greppable — check them first if
either build breaks:

- `win AgentLoop.cpp` — derives from `HostSourceBase`, `ReleaseCached()` replaces the
  `haveCached` atomic, `fi.texture` -> `fi.handle`
- `win CaptureTypes.h` / `win ScreenCapture.cpp` — `FrameInfo` is now
  `CapturedFrame<ID3D11Texture2D*>`
- `win InputInjector.cpp` — the `- 1` removals for the pointer-extent fix
- `win ViewerInput.{h,cpp}` — `PointerLockState` replaces `relative_`
- `win SessionWindow.{h,cpp}` — `AgentDriver::Poll` and `kAgentStatusPollMs`
- `win Viewer.cpp`, `mac App.swift`, `mac StreamView.swift` — `OpenViewerCount` /
  `dh_viewer_*`; `ViewerRegistry` is deleted, so both `.environment(viewers)` lines are
  gone with it
- `mac RemoteView.swift` — `DHPointerLock` drives the lock; `mouseLocked` is computed
- `mac DeskhubBridge.{h,mm}` + `MacKeyMap.swift` — `dh_map_key` ->
  `dh_native_key_to_vk`; `DHModifier` + `dh_modifier_class` moved out to `ClientFfi.h`,
  so the bridging header still sees them but through `ClientFfi.h`
- `mac MacKeyMap.{h,cpp}` + `InputInjector.mm` — `mackeys::Modifier` / `ModifierOf`
  deleted; `CurrentFlags` switches on `deskhub::ModifierKeyOf` and the file gained an
  explicit `VirtualKeys.h` include
- `mac AgentModel.swift` — the hardcoded start-failure sentence is now
  `DHStrShareStartFailed` + `DeskhubAgent.lastError`
- `win SessionWindow.cpp` — the start-failure title is `ui::kShareStartFailed`
- all three `InputInjector.h` — `LocalInputGate` base; `SetEnabled`/`enabled`/
  `SetLocalMonitor`/`enabled_`/`localMon_` deleted from each
- macOS + Windows `AgentLoop.cpp` — the `lastKeepaliveUs` line deleted from the flush
  hook
- Windows/Apple/Android decoder headers — two `static_assert`s collapsed into
  `EngineDecoder`

One deleted entry point to watch for: `dh_viewer_subtitle` is gone, replaced by
`dh_pointer_subtitle`, which takes a `DHPointerLock` instead of a bare `bool`.

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
- [x] **`platform`: `HostSourceBase<Capture, Injector, Encoder>`.**
      Lives in `platform/include/deskhubp/session/HostEngine.h` and carries
      `capture` / `injector` / `encMutex` / `encoder` — the four members
      `MakeDefaultSourcePolicy` and `MakeDefaultStatusHooks` already required
      implicitly, now declared once. All three desktop `AgentLoop.cpp` derive from it
      (Windows instantiates it with the abstract `IVideoEncoder`, which works because
      the interface has a virtual destructor).
      The `Pipeline()` down-casters were left per-client on purpose: routed through a
      shared `AsPipeline<P>()` every one of the ~15 call sites gets longer, so the
      three-line file-local alias is the smaller form.
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
- [x] **`platform`: one cached-last-frame mechanism.** Three answers to one
      requirement: macOS `cachedPb`/`ReleaseCached`, Windows `cachedTex`/`haveCached`,
      Linux pushed it into `VaEncoder::EncodeLast`/`haveSourceFrame`. The *policy* is
      identical; only the retain/release primitive differs, so `HostSourceBase` owns
      the flag and its memory ordering (`hasCachedFrame()` / `SetCachedFrame()`) while
      the handle and its retain/release stay in the derived struct. macOS keeps only
      `cachedPb` + `ReleaseCached`; Windows keeps `cachedTex` behind the same
      `ReleaseCached` shape and its `haveCached` atomic is gone; Linux mirrors
      `encoder->haveSourceFrame()` into the shared slot and clears it wherever the
      encoder is reset (rebuild, fps change, stop) — a clear Windows was missing on
      `stopCapture`.
- [x] **`core`: move `lastKeepaliveUs` bookkeeping next to `DueForFlush`.**
      New `TakeFlushReason(st, nowUs)` in `HostRouter.h` asks `DueForFlush` and stamps
      `lastKeepaliveUs` when the answer is not `None`; `DueForFlush` stays a pure
      question. `HostNetLoop.cpp` calls it, guarded by `hooks.source.flush` first so a
      client with no flush hook cannot silently restart the interval, and the
      assignment is gone from all three client files. Behaviour note: the stamp used to
      live at the *end* of each client's flush body, so a flush that returned early
      (no cached frame) left the clock stale and re-fired every tick; it is now
      throttled to one attempt per interval, which is the intended reading.
      Tested in `HostRouterTests.cpp` — keepalive and IDR both restart the interval,
      and asking without taking leaves the clock alone.
- [x] **Fix the `LOGE`/`LOGW` inconsistency** on the identical capture-start failure:
      `linux:177` and `macos:179` use `LOGE`, `windows:250` uses `LOGW`.
- [x] **Promote the Windows port-error message to the default.**
      `client/windows/cpp/AgentLoop.cpp:92-98` is strictly more informative than
      `DefaultPortError` (`platform/src/session/HostEngine.cpp:13-16`).

## P2 — client session / FFI layer

- [x] **`platform`: the five-field `ClientEngine::Start` overload.** It shipped as the
      overload, not the `ClientEngineConfig::For` factory sketched here — the config is
      an aggregate, so a named constructor bought nothing a brace-init did not.
      Android's `ClientLoop.h` was deleted when `ClientSessionAndroid.cpp` landed.
      Linux's survived one pass longer than this item claimed, because it still carried
      a `Phase` alias and a `Start(cfg, sink)` helper; once the GTK viewer moved to
      session callbacks nothing referenced `Phase` any more, so
      `client/linux/cpp/ClientLoop.h` is now **deleted** too and `ViewerWindow` holds a
      `deskhubp::ClientEngine<AvDecoder, VideoSink*>` directly, calling `SetSurface`
      before `Start`.
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
      `DeskhubBridge.mm` is down to `dh_native_key_to_vk` and the four
      `dh_has_*`/`dh_open_*` permission calls (160 -> 25 lines; `dh_modifier_class` moved
      out to `ClientFfi.cpp` in the Part 2 P1 audit). `dha_last_error` is new,
      which the shared agent drive loop below will need.
- [x] **BUG: `DHAgentStatus` silently drops `zeroCopy`.**
      `client/macos/app/cpp/DeskhubBridge.h:27-38` has no field for it, so
      `dha_status` (`DeskhubBridge.mm:128-142`) never copies
      `AgentSourceStatus::zeroCopy` (`core/include/deskhub/media/AgentTypes.h:26`).
      Linux surfaces it in the UI (`client/linux/gtk/ShareWindow.cpp:190,194`);
      macOS cannot.
- [x] **(done for the two C++ UIs as `deskhubp::AgentDriver`; Swift keeps its own)
      Unify the agent drive loop.** `platform/include/deskhubp/session/AgentDriver.h`
      owns the poll interval (`kAgentStatusPollMs`), the start-on-a-worker-thread step
      with its error marshalling, and the per-tick decision
      (`AgentDriveState::Starting | Running | Stopped`). GTK uses all of it —
      `ShareWindow` lost its own `std::thread`, its `starting_` flag and its
      hand-written `Refresh` decision. win32 uses `Poll` and the interval constant.
      Deliberately *not* unified: win32 starts the agent synchronously on the caller's
      thread while its UI runs on a worker, the exact opposite topology to GTK's, so
      sharing `StartAsync` there would be a rewrite of the threading model rather than
      a de-duplication. Swift cannot consume a C++ header at all; its copy would need
      the whole driver mirrored through `dha_*`, which is more FFI than the ~15 lines
      it would save.
- [x] **Expose the shared source labels through the FFI.** `DHSourceInfo` now carries
      `displayName` / `sizeLabel` / `pickerLabel` and `DHAgentStatus` carries `label`,
      all filled from `SourceLabel.h`. Swift's `Source` computed properties and
      `ShareView.label(for:)` are gone, and so are the per-call `dh_source_picker_label`
      / `dh_shared_source_label` buffer dances (both entry points deleted — nothing
      called them any more). Android picks the same fields up through the JNI struct
      path, which also settles the `x` vs `×` separator: `MainActivity` and
      `StreamActivity` used `×`, everyone else `x`; core's `x` wins.

## P4 — cosmetic / low priority

- [x] **`core`: `CapturedFrame<Handle>` alias template** in
      `core/include/deskhub/media/CaptureContract.h`, so the three `CaptureTypes.h`
      shrink to one instantiation plus platform extras. macOS is
      `using MacFrameInfo = deskhub::media::CapturedFrame<void*>;` and Windows
      `using FrameInfo = deskhub::media::CapturedFrame<ID3D11Texture2D*>;`, both with
      the handle spelled `handle` rather than `pixelBuffer` / `texture`; Linux derives
      from `CapturedFrame<const uint8_t*>` because it also carries dma-buf extras. The
      per-client `static_assert(CapturedFrameLike<...>, ...)` is gone — the template is
      asserted once in `CaptureContract.h`. (Windows' decoder-side `DecodedFrame` also
      has a `texture` field; it is a different struct and was left alone.)
- [x] **(done as `deskhubp::LocalInputGate<Derived>` in
      `platform/include/deskhubp/input/LocalInputGate.h`)
      Align the three `InputInjector` headers.** The shared half was never the `Send*`
      hooks — those are the CRTP surface `InputApplier` calls, and each body is
      genuinely per-OS. What was triplicated is the *gate* around them:
      `enabled_`, `localMon_`, `SetEnabled`, `enabled()`, `SetLocalMonitor`, and the
      `localMon_ && localMon_->LocalActive(NowUs())` expression every `Apply` opens
      with. All three injectors now inherit those, so `Apply` reads
      `if (!enabled() ...) return; DispatchInput(e, localUserActive());` everywhere and
      `SetEnabled` has one body with the settled release-on-disable order (macOS used
      to flip the flag before releasing; the release path never reads it, so the order
      was cosmetic). `Init` stays per-OS, as intended.
- [x] **Audit `ClientDiagCaps` per client — linux's `{}` is correct, and now provably
      so.** It never needed a run: `presentMs` is fed only from
      `PresentTimingDecoder::TakePresentDelayMs` and `dispDrop` only from
      `CongestionAwareDecoder::TakeCongestionDrops` (`ClientEngine::HarvestDecoder`),
      and `AvDecoder` models neither — it is `RenderCountingDecoder` only, because the
      GTK renderer presents on the frame clock and reports nothing back. Turning either
      column on would print a permanently empty field. So the question is answered by
      the decoder's concept set, and `ClientEngine`'s caps parameter now *defaults* to
      exactly that (`kDecoderDiagCaps`). The other three clients still pass their caps
      explicitly and are untouched; linux passing nothing is now the right answer by
      construction rather than by omission.

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
- [x] **Reconcile `AxisToAbsCoord` / `AbsCoordToPixel` with `NormalizeAxis`.** Settled
      in favour of `NormalizeAxis`: **`extent` is a pixel count everywhere**, and the
      `- 1` that turns it into a span now lives inside `PointerMap.h` instead of at the
      call sites. So Linux was the one that was wrong — passing the raw `srcW_`/`deskW_`
      through the old span-shaped API put normalized 65535 one pixel past the right
      edge of the shared display — and Windows drops the `w - 1` it was compensating
      with. Two consequences worth naming: `AbsCoordToPixel` now rounds to nearest
      rather than truncating, because flooring in both directions loses a pixel on the
      way back (a tap on pixel 1 of 1920 used to inject pixel 0); and the round trip
      `AxisToAbsCoord` -> `AbsCoordToPixel` is now exact, asserted over the edge pixels
      in `PointerMapTests.cpp` along with agreement with `NormalizeAxis` itself.
      `AbsCoordToAxis` is untouched: its `extent` is a continuous rect, not a pixel
      count, and macOS is its only caller.
- [x] **`core`: one modifier classifier.** It shipped as `ModifierKeyOf(int32_t vk)`
      returning `ModifierKey` in `VirtualKeys.h`, not the `ModifierClass` name sketched
      here. It collapses `PreferLeftModifier` (`ScancodeTable.h:10-17`) and — via FFI —
      the parallel Swift table that used to hardcode mac keycode pairs in
      `RemoteView.swift`.
      **Audited again 2026-08-01 and finished**, because the first pass left the tail of
      it in place: `mackeys::Modifier` was a fourth enum with the same six values as
      `deskhub::ModifierKey` and `DHModifier`, and `dh_modifier_class` bridged two of
      them with `DHModifier(int(mackeys::ModifierOf(vk)))` — **a raw int cast between two
      independently declared enums**, which silently yields the wrong modifier the moment
      either one is reordered or gains a value. `mackeys::Modifier` / `ModifierOf` are
      now deleted; `InputInjector.mm` switches on `deskhub::ModifierKeyOf` directly to
      build its `CGEventFlags`; and `dh_modifier_class` moved from the macOS-only
      `DeskhubBridge` into `ClientFfi.{h,cpp}` where it is an explicit switch with no
      cast — which also makes it reachable from iOS, which never had it.
- [x] **`core`: `PointerLockState`** (`core/include/deskhub/input/PointerLockState.h`)
      — `{locked, paused}` plus `OnToggleLockKey` / `OnTogglePauseKey` / `OnEscape` /
      `OnFocusLost`, each returning a `PointerLockEffect{lockChanged, pauseChanged,
      releaseHeldInput}` so the caller does the OS-specific grab and nothing else, and
      `HintText` / `SubtitleFor` / `TitleFor` over the existing `ViewerTitle.h` strings.
      Tested in `core/tests/input/PointerLockStateTests.cpp`.
      GTK (`ViewerWindow`) and win32 (`ViewerInput`) both derive their whole lock
      behaviour from it now. Adopting it on win32 also **fixed a divergence**: losing
      focus released the pointer but never released held keys, so keys could stay
      latched on the host — GTK has always done both, and the shared `OnFocusLost` does
      both.
      Swift reaches the same machine through a value-type FFI:
      `DHPointerLock{locked, paused}` in, `DHPointerLockEffect` out, over
      `dh_pointer_toggle_lock` / `_toggle_pause` / `_escape` / `_focus_lost`, plus
      `dh_pointer_subtitle` — which replaced `dh_viewer_subtitle` outright, since that
      entry point had exactly one caller and could not express `paused`.
      `RemoteVideoView` now holds a `DHPointerLock` and its `mouseLocked` is a computed
      read of it; the only thing left per-OS is the grab itself
      (`CGAssociateMouseAndMouseCursorPosition` + `NSCursor`).
      Still per-OS on purpose: the toggle key. "F9 written three ways" is three *key
      spaces*, not three spellings of one constant — `VK_F9`, `GDK_KEY_F9`, mac keycode
      `0x65` — and `core` can only own the VK one.

## P2 — FFI surface and small remaps

- [x] **`platform`: promote `dh_map_key`.** It is now
      `bool dh_native_key_to_vk(int32_t native, int32_t* vk, int32_t* scan)`, declared
      once in `deskhubp/ffi/ClientFfi.h` and implemented per OS next to that OS's key
      table — macOS in `DeskhubBridge.mm` over `mackeys::MacToWin`, Linux in
      `LinuxKeyMap.cpp`, where it also absorbs the `GdkKeycodeToEvdev` step so the
      caller passes the raw `hardware_keycode`. The GTK viewer no longer includes
      `LinuxKeyMap.h` at all. The tables themselves stay client-local: they are per-OS
      key spaces, and `platform/` would have to expose evdev and mac keycodes as one
      API to hold them.
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
- [x] **`core`: viewer-side button remap tables.** The win32 half stays a WON'T-DO:
      `WM_*BUTTON*` / `GET_XBUTTON_WPARAM` (`ViewerInput.cpp:24-27,153-164`) are
      windows.h symbols and `core/` may not include OS headers. The GDK half is done —
      `X11ButtonToMouseButton` plus the `kX11Button*` constants live in
      `input/PointerMap.h` (X11 numbering, which is what GDK reports), tested in
      `PointerMapTests.cpp` including the trap that X11 orders middle and right the
      opposite way from the wire enum. Related, and deliberately left alone: the
      `MouseButton` enum
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

- [x] **`core`: add `media/AnnexB.h`.** What shipped is narrower than the sketch below,
      because only the parts with a caller were written: `NalRef{offset, size, startCode,
      header}` with `H264NalType`/`HevcNalType`/`IsH264Vcl`/`IsH264Idr`/`IsHevcIrap`,
      `StartCodeLengthAt` (the sketch called it `NextStartCode`), `ParseAnnexB`,
      `FirstVclOffset`, `ContainsIdr(span, Codec)`, `AppendLengthPrefixed` (the
      one-NAL half of what the sketch called `AnnexBToAvcc`) and `AvccToAnnexB`.
      `SplitParameterSets` was never needed — `FirstVclOffset` answers the same question
      for every caller. The five independent scanners it replaced:
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

- [x] **(now adopted everywhere, VaEncoder included — the `bps/2` outlier is gone)
      `core`: `media/RatePlan.h`** —
      `PlanRateControl(bps, fps, rc, lowLatency) -> {frameBits, vbvBytes, vbvInitialBytes}`.
      The same formula appears four times with one outlier:
      `MfEncoder.cpp:306-308`, `NvencEncoder.cpp:115-116,174-175,191-192`,
      `VtEncoder.mm:198-206`, and `VaEncoder.cpp:614-615` which uses
      **`bps/2` (half a second) instead of one frame** — inconsistent with everyone
      else; settling it makes the outlier visible.
- [x] **(all four backends honour both fields now)
      Wire `EncoderConfig::rc` and `lowLatency` for real.** No `EncoderPreset` type was
      needed in the end: `PlanRateControl` already carries everything `lowLatency`
      decides, and `rc` is one branch each. VaEncoder was the last holdout and now
      picks its VA rate-control mode from `cfg.rc` (preferred mode, then the other, then
      `VA_RC_CQP` with the existing software controller), sets `target_percentage` to
      100 for CBR and 70 for VBR with `bits_per_second` raised to the matching peak,
      and sizes both the HRD buffer and the rate-control window from the plan instead of
      the old fixed `bitrate/2` and `window_size = 500`. The startup log now names the
      mode, e.g. `1280x720 (aligned 1280x720) @60 fps, 20.0 Mbps CBR, low latency`.
      Verified on Intel iHD: CBR/VBR x low-latency on/off all produce a decodable
      stream, and a mid-stream `SetBitrate` is honoured.
- [x] **(now used by all four, the linux `VideoRenderer` copy included)
      `core`: `media::PresentCounters` value type** (`Add`, `TakeCount`,
      `lastPtsUs`, `TakePresentDelayMs`). Hand-rolled four times:
      `WinVideoDecoder.cpp:57-67`, `MediaCodecDecoder.h:27-31` + `.cpp:160-164`,
      `VtDecoder.h:25-33` + `VtDecoderApple.mm:202-206` (byte-identical to
      MediaCodec's), `VideoRenderer.h:28-33`. `ClientEngine::HarvestDecoder`
      (`ClientEngine.h:249-272`) already consumes them uniformly.
- [x] **`core`: expose the small math helpers.** The last `& ~1u` (in
      `linux ScreenCapture.cpp`) is now `deskhub::EvenDown`. The two H.264 helpers went
      into one new header, `core/include/deskhub/media/H264Encode.h`, rather than the
      separate `H264Level.h` sketched here — they are both macroblock geometry off the
      same spec and have the same single caller:
      `AlignEncodeSize(w, h, align)` returns the padded size, the macroblock counts and
      the SPS crop offsets, which kills the `alignedW_`/`alignedH_`/`mbW_`/`mbH_`
      quartet and all three copies of the `(aligned - real) / 2` crop expression; and
      `LevelFor(mbW, mbH, fps)` is the spec table lifted verbatim out of the anonymous
      namespace. Tested in `core/tests/media/H264EncodeTests.cpp`, and confirmed on
      hardware by encoding 1918x1078 and decoding it back to 1918x1078.
      The CQP controller stays in `VaEncoder.cpp`: it is one driver's fallback for
      hardware with no CBR, it reads `cqpMode_`/`packedHeaders_`/`lastIdrBytes_` from
      the encode loop it lives in, and no other backend has anything like it.

## P2 — name the interfaces that already exist

- [x] **(the header move is a WON'T-DO; the two useful sub-items are done)
      Move `IVideoEncoder`/`IVideoDecoder` to `platform/include/deskhubp/media/`.**
      The premise was wrong: `grep` says `IVideoEncoder.h` and `IVideoDecoder.h` are
      included by **Windows files only** — Linux and Apple never had them, because they
      satisfy the `VideoContract.h` concepts directly instead of inheriting an
      interface. So this is not a whole-or-nothing cross-platform move at all, and
      moving a header whose `Init` takes `ID3D11Device*` into `platform/` would break
      the same one-API-everywhere rule that already sent `D3D11VideoProcessor` to
      `client/windows/cpp/gpu/` under P3. They stay where they are.
      Of the three sub-items: `IsOpen()` on the encoder was already done; the
      `EngineDecoder` bundle is done —
      `EngineDecoder<D, Surface> = VideoDecoderLike && RestartableDecoder &&
      SurfaceBoundDecoder` now lives in `VideoContract.h`, replaces the three-clause
      `requires` on `ClientEngine`, and collapses two `static_assert`s into one in each
      of the four decoder headers.
      The `Finish()` -> `Close()` rename is **dropped**, and the item's description of it
      was also wrong: `VtEncoder::Finish` invalidates and releases the session, so macOS
      destroys exactly like Linux does. Windows is the lone outlier — `MfEncoder::Finish`
      drains the MFT and leaves it alive — and `Finish` is the name the shared
      `VideoEncoderLike` concept requires, so renaming Linux alone would break the
      contract while renaming all three would only paper over a Windows-side lifetime
      question that cannot be tested here.
- [x] **(NVENC now refuses a crop mismatch and falls back to MF; the false test
      comment is fixed)
      BUG: `EncoderConfig::srcWidth/srcHeight` contract is false.**
      `ContractTests.cpp:195-196` claims "every platform reads it that way"; only
      `MfEncoder.cpp:344` reads it, and NVENC registers the input texture at
      `width/height` (`NvencEncoder.cpp:211-212`) — a genuinely different
      `srcWidth` would corrupt NVENC output. Fix the contract or the backends.
- [x] **`EncoderConfig::codec` was silently ignored.** Settled as "refuse what you
      cannot emit": `VtEncoder::Init` fails with a named codec instead of quietly
      producing H.264, the triplicated `codec == HEVC ? "HEVC" : "H264"` ternary is now
      `deskhub::media::CodecName` (adopted in MF/NVENC/MfDecoder), and `VaEncoder::Init`
      now carries the same guard — it configures `VAProfileH264Main` and nothing else,
      so anything but H.264 is refused before a VA object is created.
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
      **Audited again 2026-08-01 — one had been missed**, and it was the worst of them:
      `AgentModel.swift` still spelled the port out inside a sentence that *guessed* why
      sharing failed ("No display was found, the display may be disconnected, or UDP
      port 47777 is already in use"), while GTK and win32 both show the engine's actual
      `LastError()` — which already distinguishes those cases and interpolates
      `kDeskhubPort`. macOS now shows `DeskhubAgent.lastError` too, so all three report
      the real reason, and the last hardcoded `47777` outside `Wire.h` is gone. The
      shared title came with it: `ui::kShareStartFailed` / `DHStrShareStartFailed`
      replaces the three copies of "Could not start sharing".
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

- [x] **(Apple, Android and now GTK — no client polls for status any more)
      Retire the 500 ms status-poll loops in favour of session callbacks.** The GTK
      viewer was the last one. It does not go through the C FFI — it holds a
      `deskhubp::ClientEngine` directly — so it fills `ClientEngineConfig::onStatus` /
      `onParams` / `onEnded` / `onFinished` instead, which are the same callbacks
      `DHSessionCallbacks` is built on. `ViewerWindow` lost `statusTimer_` and
      `OnStatusTimer` entirely; the title now redraws when a status window closes and
      the window resizes itself when the stream size is negotiated, rather than both
      being rechecked twice a second. The engine fires these on its net thread, so each
      one hops to the GTK main loop through a `shared_ptr<ViewerWindow*>` token that
      `~ViewerWindow` nulls — the same shape as Apple's retained box, and safe for the
      same reason: `ClientEngine::Stop` joins both threads before any member dies.
      `ClientLoop::Start` now takes the whole config, so the five-field overload it
      existed to wrap is gone.
      For the record, the earlier halves: `StreamModel` no longer runs a `Timer`:
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
      Nothing polls for status any more.
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
- [x] **`core`: last-viewer-closed counter.** The same invariant was written in three
      languages — `macos App.swift` + `StreamView.swift`, `MainWindow.cpp`
      (`openViewers_`), `Viewer.cpp` (`g_openFrames`). Now
      `deskhub::OpenViewerCount` in `core/include/deskhub/session/OpenViewers.h`:
      `Opened()`, and a `Closed()` that returns *true only for the close that empties
      the app*, which is the whole question every caller was asking. Tested in
      `core/tests/session/OpenViewersTests.cpp`, including that an unbalanced close
      cannot drive the count negative — the old `--n <= 0` forms could, and a negative
      count means the next viewer to close is wrongly reported as the last.
      GTK and win32 hold an instance; Swift reaches a process-wide one through
      `dh_viewer_opened` / `dh_viewer_closed` / `dh_viewer_count`, which let
      `ViewerRegistry` and its two `.environment(viewers)` injections be deleted
      outright. The earlier objection — that an FFI round-trip costs more than the rule
      — was answered by the fact that the same C surface was being added anyway for
      `PointerLockState`, so the marginal cost is three one-line entry points.

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
