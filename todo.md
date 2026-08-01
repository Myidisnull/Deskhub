# TODO — de-duplicate the client layer

Four parts, one per subsystem:

1. Session/agent loop + FFI
2. Input / keymap
3. Video encode / decode / render
4. UI / app-model

Within each part, items are ordered by value-per-risk (P0 first). Run `make test`
and `make lint` after each item. Items marked **BUG** are behavioural defects found
while comparing the copies, not just duplication.

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
- [ ] (needs macOS) **macOS: unify the retarget path.** `client/macos/app/cpp/capture/ScreenCapture.mm:135-138`
      and `:227` inline `FitStreamSize` + `ApplyQualityScale`, i.e. re-implement
      `deskhub::RetargetStream` (`core/src/session/HostRouter.cpp:44-49`) inside a
      capture backend. Decide: either call `RetargetStream` and pass the result down,
      or keep capture-side rescale but drive it from the shared result.

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
- [ ] (needs macOS + Linux builds — all three AgentLoops must move together)
      **`platform`: `HostSourceBase<Capture, Encoder>`.** Carries
      `capture` / `injector` / `encMutex` / `encoder`, the two `Pipeline()`
      down-casters (byte-identical: `linux:49-55`, `macos:61-67`, `windows:67-73`),
      and the cached-last-frame protocol. Collapses `linux:22-57`,
      `macos:22-69`, `windows:35-75`.
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
- [ ] (needs macOS + Linux builds) **`platform`: one cached-last-frame mechanism.** Three answers to one
      requirement: macOS `cachedPb`/`ReleaseCached` (`macos:42-50,151,167-169,190,242-247`),
      Windows `cachedTex`/`haveCached` (`windows:55-56,194-195,225-239,303-308`),
      Linux pushes it into `VaEncoder::EncodeLast`/`haveSourceFrame`
      (`client/linux/cpp/encode/VaEncoder.h:34-38`). The *policy* is identical; only
      the retain/release primitive differs -> customization point on `HostSourceBase`.
- [ ] (behaviour-sensitive — do only with all three desktop builds at hand)
      **`core`: move `lastKeepaliveUs` bookkeeping next to `DueForFlush`**
      (`core/include/deskhub/session/HostRouter.h:47`). Currently assigned from three
      client files: `linux:239`, `macos:247`, `windows:309`.
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
- [ ] (needs Android build) **Android: stop forking the forwarders.**
      `client/android/app/src/main/cpp/JniBridge.cpp:142-198` re-implements all 12
      operations of `DESKHUB_DEFINE_CLIENT_SESSION_FORWARDERS`
      (`platform/include/deskhubp/ffi/ClientSessionForward.h:6-66`) against a global
      `std::unique_ptr<ClientLoop> g_client` (`JniBridge.cpp:21`).
      Preferred: link the `dh_session_*` ABI and reduce `JniBridge.cpp` to thin
      JNI -> C shims, killing the global. (~60 LOC -> ~15)
      Side effect: android currently lacks `dh_session_key` (down/up) and
      `dh_session_release_all_input` purely as a fork artifact — this restores them.

## P3 — agent FFI and the UI-side drive loop

- [ ] (needs macOS — the only `dha_*` consumer today is the mac Swift app)
      **`platform`: add `deskhubp/ffi/AgentSession.h` + `DESKHUB_DEFINE_AGENT_SESSION_FORWARDERS`.**
      `client/macos/app/cpp/DeskhubBridge.mm:46-144` (`dha_default_options`,
      `dha_list_share_sources`, `dha_start`, `dha_stop`, `dha_running`, `dha_status`,
      `dha_local_addresses`) is ~99% generic — `ListDisplays()`
      (`platform/include/deskhubp/media/DisplayEnum.h:9`) and `ListLocalIPv4()`
      (`deskhubp/net/NetInfo.h`) are already platform APIs. Only `dh_map_key` and the
      `dh_has_*` permission calls are genuinely macOS.
- [x] **BUG: `DHAgentStatus` silently drops `zeroCopy`.**
      `client/macos/app/cpp/DeskhubBridge.h:27-38` has no field for it, so
      `dha_status` (`DeskhubBridge.mm:128-142`) never copies
      `AgentSourceStatus::zeroCopy` (`core/include/deskhub/media/AgentTypes.h:26`).
      Linux surfaces it in the UI (`client/linux/gtk/ShareWindow.cpp:190,194`);
      macOS cannot.
- [ ] (needs macOS + Linux) **Unify the agent drive loop.** Same sequence in three places — start, show
      `LastError()` on failure, poll `Status()` on a 300-500 ms timer, tear down when
      `!running()`:
      `client/windows/win32/SessionWindow.cpp:48-56,182-201`,
      `client/linux/gtk/ShareWindow.cpp:134-166`,
      `client/macos/app/cpp/DeskhubBridge.mm:93-117` + `swift/DeskhubAgent.swift:65-86`.
      (~40 LOC x 3)
- [ ] (needs macOS — `dh_shared_source_label` exists, the remaining work is wiring the
      agent-status structs) **Expose the shared source labels through the FFI.**
      `client/apple/swift/ClientSession.swift:22-24` re-implements
      `SourceName`/`SourceSizeLabel`/`SourcePickerLabel`
      (`core/include/deskhub/media/SourceLabel.h:12-20`) in Swift, and
      `client/macos/app/swift/ShareView.swift:37` re-implements `SharedSourceLabel`
      (`:22-30`) — which windows (`SessionWindow.cpp:26`) and linux
      (`ShareWindow.cpp:182`) both call correctly. Add a formatted-label field to
      `DHSourceInfo` / `DHAgentStatus` instead of raw fields only.

## P4 — cosmetic / low priority

- [ ] (needs macOS + Linux to verify the three CaptureTypes.h together)
      **`core`: `CapturedFrame<Handle>` alias template** in
      `core/include/deskhub/media/CaptureContract.h`, so the three `CaptureTypes.h`
      shrink to one instantiation plus platform extras:
      `client/linux/cpp/capture/CaptureTypes.h:19-34`,
      `client/macos/app/cpp/capture/CaptureTypes.h:6-12`,
      `client/windows/cpp/capture/CaptureTypes.h:10-16`.
      All three repeat the same `static_assert(CapturedFrameLike<...>, ...)`.
- [ ] (needs macOS + Linux) **Align the three `InputInjector` headers.** Everything except `Init`'s
      signature is already the same interface across
      `client/linux/cpp/input/InputInjector.h`,
      `client/macos/app/cpp/input/InputInjector.h`,
      `client/windows/cpp/input/InputInjector.h` (~25 declaration lines x 3).
      Consider a `deskhub::InputApplier`-side concept so the shared members are
      declared once and only `Init` stays per-OS.
- [ ] (needs a Linux run to confirm intent) **Audit `ClientDiagCaps` per client** — linux passes the default `{}`
      (`ClientLoop.h:7`) while android `{false,true}`, windows `{true,false}`,
      apple `{false,true}`. Confirm linux's is intentional and not an omission.

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

- [ ] (needs iOS + Android builds) **`core`: `input/TrackpadCursor.h`** — normalized cursor + `ClampToVisible`
      + `MoveBy(dx, dy)` + emit `(nx, ny)`, exposed as `dh_cursor_*` in the FFI.
      `client/ios/app/swift/TouchInputView.swift:101-208` and
      `client/android/.../StreamActivity.kt:653-757` are the same algorithm written
      twice (~200 LOC): `clampToVisible` bodies are direct transliterations
      (`TouchInputView.swift:112-126` vs `StreamActivity.kt:675-691`); both
      hardcode `65535` (`:147-148` / `:670-671`).
- [ ] (needs iOS + Android builds) **BUG-ish: mobile normalization divides by `extent`, core by `extent - 1`.**
      `NormalizeAxis` (`core/src/media/ViewFit.cpp:76-80`) uses `extent - 1`; the
      two mobile trackpads multiply by `65535 / extent`. Off-by-one at the far edge
      and two different definitions of "normalized". `TrackpadCursor` settles it.
- [ ] (constant merge is done — `kNormalizedMax` now aliases `kAbsCoordMax`; the
      `extent-1` vs `extent` reconciliation remains and needs iOS + Android)
      **`core`: collapse `kAbsCoordMax` and `kNormalizedMax`** — the same 65535
      under two names inside core (`input/PointerMap.h:6`, `media/ViewFit.h:6`),
      and reconcile `NormalizeAxis` (`extent-1`) with `AxisToAbsCoord` (`extent`,
      `PointerMap.h:24-30`).
- [x] **`core`: `ModifierClass(int32_t vk)`** in `VirtualKeys.h`. Collapses
      `mackeys::ModifierOf` (`MacKeyMap.cpp:101-123`), `PreferLeftModifier`
      (`ScancodeTable.h:10-17`), and — via FFI — the parallel Swift table at
      `client/macos/app/swift/RemoteView.swift:127-136` (hardcodes the same mac
      keycode pairs as `MacKeyMap.cpp:46-53`; drifts silently if the C++ table
      changes).
- [ ] (needs macOS + Linux — the win32 half must land together)
      **`core`: `PointerLockState`** — `{locked, paused}` + `OnToggleKey()` /
      `OnFocusLost()` / `HintText()`. Three unrelated ~20-LOC state machines:
      `client/windows/win32/ViewerInput.cpp:71-89,115-118,177-179`,
      `client/linux/gtk/ViewerWindow.cpp:229-268,353-358`,
      `client/macos/app/swift/RemoteView.swift:86-103,179-183`. Also unifies the
      three divergent locked-hint strings (`Viewer.cpp:67`, `ViewerWindow.cpp:195`,
      `StreamView.swift:57-58`) and the F9 constant
      (`ViewerInput.cpp:17`, `ViewerWindow.cpp:18`, `RemoteView.swift:100` `0x65`).

## P2 — FFI surface and small remaps

- [ ] (needs macOS + Linux) **`platform`: promote `dh_map_key`** (macOS-only,
      `client/macos/app/cpp/DeskhubBridge.h:13`, `.mm:28-31`) into
      `deskhubp/ffi/ClientFfi.h` as a per-OS `dh_native_key_to_vk(native, *vk, *scan)`.
      The GTK viewer then uses the same entry point instead of calling
      `linuxkeys::EvdevToWin` directly (`ViewerWindow.cpp:270-273`).
- [ ] (needs Android build) **One FFI marshalling style.** `JniBridge.cpp:61-73` (hotkeys) and `:42-57`
      (sources) hand-build tab-separated strings re-parsed in
      `NativeClient.kt:201-211,223-228`, while iOS gets `DHHotkey`/`DHSourceInfo`
      structs (`ClientFfi.h:17-43`). Pick the struct path, delete ~40 LOC.
- [ ] (needs iOS + Android — the only two hotkey-bar clients)
      **`core`: `HotkeyDispatch(queue, hotkey)`** next to `Hotkey::hasModifier()`
      (`Hotkeys.h:17-19`, currently used by nobody) — removes the duplicated
      `modVk != 0 ? keyChord : keyTap` rule at
      `client/ios/app/swift/StreamView.swift:245-254` and
      `StreamActivity.kt:582-588`.
- [ ] (GTK literal is done; the `RemoteView.swift` 120s need macOS, the missing wheel
      path needs iOS + Android) **Use `WheelNotches`/`kWheelDeltaPerNotch`** (`PointerMap.h:7,32-37`) instead
      of the literal 120 at `client/linux/gtk/ViewerWindow.cpp:21,342-346` and
      `client/macos/app/swift/RemoteView.swift:172-177`. Gap while there: Android
      and iOS have **no wheel path at all** (no `nativeMouseWheel`; iOS wraps
      `dh_session_mouse_wheel` in `ClientSession.swift:98-100` but never calls it).
- [ ] (win32 half is a WON'T-DO — `WM_*`/`GET_XBUTTON_WPARAM` are windows.h symbols,
      which core forbids; the GDK half is plain ints and needs Linux)
      **`core`: viewer-side button remap tables.** GDK button -> `MouseButton`
      (`ViewerWindow.cpp:318-326`) and WM_*BUTTON* -> `MouseButton`
      (`ViewerInput.cpp:24-27,153-164`) are pure constant maps. Related: the
      `MouseButton` enum (`protocol/Wire.h:140-144`) is re-declared truncated in
      `client/apple/swift/ClientSession.swift:10-14` (3/5 values) and
      `NativeClient.kt:50-51` (2/5 values).

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

- [x] **(core + tests + MfEncoder/NvencEncoder done; adopting it in `VaEncoder.cpp:614-615`
      needs Linux and `VtEncoder.mm:198-206` needs macOS)
      `core`: `media/RatePlan.h`** —
      `PlanRateControl(bps, fps, rc, lowLatency) -> {frameBits, vbvBytes, vbvInitialBytes}`.
      The same formula appears four times with one outlier:
      `MfEncoder.cpp:306-308`, `NvencEncoder.cpp:115-116,174-175,191-192`,
      `VtEncoder.mm:198-206`, and `VaEncoder.cpp:614-615` which uses
      **`bps/2` (half a second) instead of one frame** — inconsistent with everyone
      else; settling it makes the outlier visible.
- [x] **(MF + NVENC now honour both fields; VaEncoder needs Linux, VtEncoder needs macOS)
      Wire `EncoderConfig::rc` and `lowLatency` for real.** Declared at
      `core/include/deskhub/media/VideoTypes.h:25-26`, blessed by
      `ContractTests.cpp:192-193`, **read by zero backends** — every encoder
      hardcodes its own dialect of infinite-GOP / no-B / CBR / low-latency
      (`MfEncoder.cpp:300-305`, `NvencEncoder.cpp:102-121`,
      `VtEncoder.mm:167-174`, `VaEncoder.cpp:240-266,559-560`). Add a
      `deskhub::media::EncoderPreset` and make backends consume it.
- [x] **(core type + WinVideoDecoder done; MediaCodecDecoder needs Android,
      VtDecoder needs macOS/iOS, VideoRenderer needs Linux)
      `core`: `media::PresentCounters` value type** (`Add`, `TakeCount`,
      `lastPtsUs`, `TakePresentDelayMs`). Hand-rolled four times:
      `WinVideoDecoder.cpp:57-67`, `MediaCodecDecoder.h:27-31` + `.cpp:160-164`,
      `VtDecoder.h:25-33` + `VtDecoderApple.mm:202-206` (byte-identical to
      MediaCodec's), `VideoRenderer.h:28-33`. `ClientEngine::HarvestDecoder`
      (`ClientEngine.h:249-272`) already consumes them uniformly.
- [ ] (needs Linux + macOS — every remaining call site is in VaEncoder or the mac
      ScreenCapture) **`core`: expose the small math helpers.** `Even()` (`StreamSize.cpp:7-10`)
      is copied verbatim into `mac ScreenCapture.mm:91-94`; `& ~1u` appears at
      `mac AgentLoop.cpp:139`, `linux ScreenCapture.cpp:220-221`,
      `Downscaler.cpp:38-41`. Add `AlignEncodeSize(w, h, align)` for VA's 16-px
      macroblock alignment (`VaEncoder.cpp:203-206`; crop-offset expression
      duplicated at `:109-111` and `:572-574`), `H264Level.h::LevelFor`
      (`VaEncoder.cpp:41-65`, pure spec table), and consider lifting the CQP
      controller (`VaEncoder.cpp:350-373,773-796`) next to `BitrateController.h`.

## P2 — name the interfaces that already exist

- [ ] (needs all three desktop builds; `IsOpen()` on the encoder is already done)
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
- [ ] (needs macOS + Linux + Android — the ignoring backends live there)
      **`EncoderConfig::codec` is silently ignored** by VT
      (`VtEncoder.mm:141`), VA (`VaEncoder.cpp:240`), libav (`AvDecoder.cpp:33`),
      MediaCodec (`MediaCodecDecoder.cpp:11`) — all hardcode H.264; only MF/NVENC
      honour it. Either add `SupportedCodecs()` to the contract or drop the field.

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

- [x] **(adopted in GTK + win32 + macOS Swift; the iOS/Android-only pair — "Session
      ended"/"Connecting to..." — still lives in Swift/Kotlin)
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

- [ ] (needs macOS + Android + Linux; the win32 300 ms agent-timer outlier is already
      unified to 500 ms) **Retire the 500 ms status-poll loops in favour of `DHSessionCallbacks`**
      (already in `ClientSession.h:14-23`, already used by win32
      `Viewer.cpp:182-208`). Four viewer-side copies:
      `client/apple/swift/StreamModel.swift:120-147`,
      `StreamActivity.kt:178-196`, `ViewerWindow.cpp:115,183-192`,
      `Viewer.cpp:219` (title only). Agent-side trio stays polling for now but
      unify the interval: `SessionWindow.cpp:19-20` uses **300 ms**, everyone else
      500 ms (`AgentModel.swift:86-104`, `ShareWindow.cpp:150-167`).
- [ ] (needs macOS + iOS; the trim/validation divergence itself is already fixed via
      `DeskhubClient.normalizedAddress`) **Shared Apple `ConnectFlow` model** in `client/apple/swift/` holding
      `address`/`isConnecting`/`connectError` + the `lastAddress` `UserDefaults`
      round-trip. Divergence today: iOS trims the address
      (`ios SessionModel.swift:21-26`), macOS does not
      (`macos SessionModel.swift:11-15`) — `" 192.168.1.10 "` fails on macOS only.
- [x] **(title helpers done — `ViewerBaseTitle`, `kViewerLockedHint`,
      `ViewerStatusWithHint`, FFI `dh_viewer_base_title`/`dh_viewer_subtitle`; the
      `kZoomedThreshold`/`kViewerMarginPx` constants remain)
      `core`: viewer-title helpers + constants.** `ViewerTitle.h` gains
      `ViewerBaseTitle(name)` and `kViewerLockedHint`; macOS re-implements
      `ComposeViewerTitle`, `kViewerConnectingStatus`, and the lock hint in Swift
      (`StreamView.swift:54-60`), and the base title "Deskhub - viewing[: name]"
      is built independently at `StreamView.swift:48-52`, `ViewerWindow.cpp:77-78`,
      `Viewer.cpp:168-169`. Add `kZoomedThreshold` (1.01:
      `ViewTransform.swift:9`, `StreamActivity.kt:236`) and `kViewerMarginPx`
      (48: `ViewerWindow.cpp:212-213`, `Viewer.cpp:89-90`) next to `ViewFit.h`.
- [ ] (needs macOS) **Drive the macOS quality picker from `kQualityPresets`**
      (`QualityPreset.h:15-20`) instead of hardcoded 720p/1080p/1440p/Native in
      `ConnectView.swift:85-90`, the way GTK (`MainWindow.cpp:221-224`) and win32
      (`MainMenuWindow.cpp:291-300`) do. Audit macOS's `max(640, maxDim)` floor
      (`AgentModel.swift:50-52`) — exists nowhere else.
- [ ] (needs macOS + Linux — the three implementations must move together)
      **`core`: last-viewer-closed counter.** Same invariant in three languages:
      `macos App.swift:4-7` + `StreamView.swift:31-38`,
      `MainWindow.cpp:370-373` (`openViewers_`), `Viewer.cpp:26,139`
      (`g_openFrames`). Small, but it is a state machine, not view code.

## P3 — mobile HUD rules

- [ ] (needs iOS + Android builds) **Extract the shared iOS/Android HUD rules** (pure logic repeated pairwise):
      pan-mode auto-follows zoom (`StreamView.swift:56-58` /
      `StreamActivity.kt:238-239`), reset transform on source switch
      (`StreamView.swift:53-55` / `StreamActivity.kt:241-244`), zoom pill `"%.1fx"`
      (`StreamOverlays.swift:52` / `StreamActivity.kt:367`), session-ended overlay
      (`StreamOverlays.swift:24-37` / `StreamActivity.kt:626-651`), "Display"
      button only when >1 source (`StreamView.swift:217` /
      `StreamActivity.kt:603`), host title format (`StreamView.swift:72-75` /
      `StreamActivity.kt:547`). Most of these become trivial once `dh_string` and
      `TrackpadCursor` (Part 2) exist.

## Must stay per-client (do not touch)

All SwiftUI/Compose/GTK/win32 layout and widget code; NSEvent/RAWINPUT/GDK event
translation; video surface plumbing (`VideoLayerView.swift`, `GtkGLArea`,
`SurfaceHolder`, child video HWND); macOS permissions
(`AgentModel.refreshPermissions`, `ConnectView.swift:38-47,141-153`); Windows
elevation/firewall (`ElevatedShare.cpp`, `MainMenuWindow.cpp:110-132`); GTK
portal/monitor geometry; Android lifecycle/Intent marshalling; the
one-window-per-source vs one-screen topology difference.
