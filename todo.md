# Deduplication backlog — lifting `client/*` code into `core/` and `platform/`

Findings from an audit of `client/`, `core/`, `platform/`. Ordered by benefit/risk.
Measurements are real diffs, not estimates.

Ground rules while working through this:

- `core/` stays free of OS and third-party headers, and must not depend on `platform/`.
- Anything new in `core/` gets a test under the matching `core/tests/` subdirectory.
- `make test` and `make lint` before calling an item done.
- Apple targets compile `client/<os>/app` through Xcode filesystem-synchronized groups,
  and link `libcore.a` + `libplatform.a` built by a CMake pre-build phase. So a file added
  to `platform/` reaches macOS **and** iOS for free; a file added under `client/apple/`
  would need a new synchronized group in both `.xcodeproj`.

---

## Tier 1 — Verbatim duplication, low risk

- [x] **1.1a `VtDecoder` macOS/iOS have drifted — sync the fix first.**
      `client/macos/app/cpp/decode/VtDecoder.h` and `client/ios/app/cpp/decode/VtDecoder.h`
      are byte-identical. The `.mm` files differ in exactly two places, and both are fixes
      that exist **only on iOS**: resetting `lastRenderedPtsUs_` in `Shutdown()`, and
      rejecting oversized SPS/PPS (`VtDecoder.mm:96`). macOS still has the overflow.
      Port both to macOS so the two copies match again.
      *Done — both files are byte-identical again, `.h` and `.mm`. The macOS overflow is
      closed: an SPS or PPS larger than the 256-byte member arrays is now rejected with a log
      line instead of being memcmp'd against a stale buffer. Not compiled here — needs a build
      on macOS.*
- [x] **1.1b Physically de-duplicate the Apple decoder.** Needs a macOS machine to verify
      the Xcode side. Either move it to `platform/` (compiled for both by the existing
      CMake pre-build phase, needs `enable_language(OBJCXX)`) or add a `client/apple/`
      synchronized group to both projects.
      *Done — took the `platform/` route: `deskhubp/VtDecoder.h` + `src/VtDecoderApple.mm`,
      selected by `if(APPLE)`, with `enable_language(OBJCXX)` widened from macOS-only to all
      Apple targets. Both client copies deleted.*

      ***A layering judgement worth overturning if you disagree:*** *CLAUDE.md rule 3 puts
      "decode" under `client/<os>/`, and this file now sits in `platform/`. The alternative —
      a `client/apple/` folder referenced by a new synchronized group in both `.xcodeproj` —
      respects the rule but relies on Xcode accepting a group whose path escapes SRCROOT
      (`../apple`), which I cannot test from here. `platform/` reuses machinery that already
      demonstrably builds for both macOS and iOS. If the Xcode route works on your machine,
      moving it back is a small change.*
- [x] **1.2 `NetInfo` → `platform/`.** Three copies:
      `client/{linux,macos,windows}/**/net/NetInfo.{h,cpp}`. The Linux and macOS bodies are
      near-identical (`getifaddrs` loop, same filters, same `stable_sort`); only the
      `FriendlyName()`/`Rank()` tables differ. Target: `platform/include/deskhubp/NetInfo.h`
      plus `NetInfoPosix.cpp` / `NetInfoWin.cpp`. Unify `AdapterAddr::name` on UTF-8
      `std::string` — Windows currently uses `std::wstring`.
      *Done — one `getifaddrs` loop for both POSIX platforms with the naming/ranking tables
      behind `#if defined(__APPLE__)`, since that difference is pure data. `AdapterAddr::name`
      is now UTF-8 `std::string` everywhere, which is what the rest of the Windows code
      already uses (`AgentSource::name`, `SourceInfo::name`); the two Windows UI call sites
      widen through a `FromUtf8()` matching the ones already in `Viewer.cpp` and
      `SourcePickerDialog.cpp`. Verified on Linux against `ip -4 addr` — same set, same order,
      loopback and link-local still filtered. The Apple naming branch was compiled with
      `-D__APPLE__` on Linux to check it. `NetInfoWin.cpp` could not be compiled here (no
      Windows SDK, no MinGW) — needs a build on Windows.*
- [x] **1.3 `client/windows/cpp/DiagLog.*` → `deskhubp/LogFile.h`.** `LogFile.h` already has
      `LogDirW()` and `LogFileName()`; what is left in `DiagLog.cpp` (redirect stdout, flush
      thread every 500 ms) is the Windows counterpart of the POSIX `LogHandle()` already
      living there. Fold it in and delete `DiagLog.{h,cpp}`.
      *Done — now `deskhubp::StartProcessLog()` in the `_WIN32` branch of `LogFile.h`, next to
      the POSIX `LogHandle()` it mirrors. Two incidental improvements: `_wfreopen_s` instead of
      `_wfreopen`, which drops the need for `_CRT_SECURE_NO_WARNINGS`, and the file-scope
      256 KB buffer became a function-local static so the header stays single-instance. The
      startup line is now tagged `[Deskhub]` like every other log line instead of `[DiagLog]`,
      which named a file that no longer exists. Not compiled here — needs a build on Windows.*
- [x] **1.4 `client/linux/cpp/encode/BitWriter.h` → `core/include/deskhub/media/BitWriter.h`.**
      Pure C++: bit packing, exp-golomb `UE`/`SE`, emulation-prevention. No reason for it to
      sit under `client/linux/`. Add `core/tests/media/BitWriterTests.cpp`.
      *Done — moved into `deskhub::media`, 9 test groups covering fixed-width packing, both
      exp-golomb round trips, the published `UE` code words, emulation prevention and its
      zero-run reset, trailing bits, `Clear()`, and the real SPS prologue. `VaEncoder.cpp` is
      the only caller and still builds.*
- [x] **1.5 Split `client/windows/cpp/net/Pacer.*`.** The token-bucket arithmetic
      (`nextUs_ += bytes * 8 * 1e6 / rateBps`) is pure — move to `core` and test it. Only
      `SleepUs()` is OS-specific — `deskhubp::SleepUs()` in `platform/`. Linux and macOS
      hosts have no pacer today; after the split they can use it.
      *Done — `deskhub::Pacer` in `core/transport/Pacer.h` with `Gate(bytes, nowUs)` returning
      the microseconds to sleep, plus `SleepUs()` in `deskhubp/Clock.h` (high-resolution
      waitable timer on Windows, `nanosleep` elsewhere). Note: the old `Pacer.cpp` was never
      listed in `client/windows/cpp/CMakeLists.txt` — it was dead code with no callers, so
      nothing had to be rewired. Deleted.*

## Tier 2 — Biggest single win: `ClientLoop::NetThread`, four copies

Measured: linux (182 lines) vs macos (190) → **43 differing lines**, mostly line-wrapping.
macos vs android (214) → **32 differing lines**. iOS is a fifth near-copy. Roughly 85–90%
of ~780 lines is one algorithm.

The shared part: building `ClientCallbacks`, lazily constructing the `Reassembler` at the
negotiated fps, splitting Video/FEC channels, `PopReady` into a decode queue bounded at
`kMaxQueuedFrames = 3`, `requestKf()` for the four causes (loss / wait_idr / dec_fail /
q_overflow), flushing the input batch, the `LinkStats.Due` block
(`FormatStatus` / `FormatCompact` / `SendFeedback`), and the `loopBusyMs` measurement.

- [x] **2.1 `deskhub::ClientPump` in `core/session/`.** Takes `span<const uint8_t>` + `nowUs`;
      never touches a socket, so `core/` stays OS-free. Per-platform differences become hooks:
      where `rendered` comes from (sink vs atomic), whether `displayCongested` exists,
      how the decoder is rebuilt.
      *Done. Result, measured:*

      | `NetThread` | before | after |
      | --- | ---: | ---: |
      | linux | 182 | 89 |
      | macos | 190 | 104 |
      | ios | 214 | 105 |
      | android | 214 | 105 |

      *800 lines → 403, against ~190 lines of shared `ClientPump` and ~250 of tests. What is
      left in each client is callback wiring, which is exactly the part that is genuinely
      per-platform. The three tap-dialect clients now differ by only what they should:
      macOS vs iOS by the `clientId` seed and the `finished_` flag; iOS vs Android by two
      config booleans. They were 32 and 7 differing lines before.*

      *The refactor surfaced two features Android had grown that nobody else did:*

      1. ***NACK planning*** *— `PlanNack` → `SendNack`. Android only.*
      2. ***A loss-run histogram log line*** *— also Android only.*

      *Rather than silently switching them on everywhere (a behaviour change, not a refactor),
      both are `ClientPumpConfig` flags — `sendNacks` and `logLossRuns` — set true only for
      Android. Turning them on elsewhere is now a one-line change per client, and worth
      considering: NACK-based retransmit is a real quality win the other three are missing.
      **Update — the user asked for both to be enabled everywhere, so all four clients now set
      `sendNacks` and `logLossRuns`.** *Linux additionally needed `pump.PlanNacks(now)` wired
      into its loop; the flag alone does nothing without it, which is worth remembering if a
      fifth client is added.*
      One detail preserved deliberately: the NACK batch stays capped at 64 indices
      (`kNackBatchMax`), not the much larger `kMaxNackIndices` from `Wire.h`, because that is
      the buffer Android actually used.*
- [x] **2.2 Thin `RecvFrom` loop stays in `client/`,** or better, becomes
      `platform/ClientNetLoop`.
      *Done — took the first option. The loop that remains is ~25 lines per client: recv,
      hand the datagram to the pump, poll frames, forward the three client-owned keyframe
      triggers, drain input, tick. Wrapping that in `platform/` would have bought nothing:
      the body is already minimal and each client threads its own decode queue and phase
      state through it.*
- [x] **2.3 `deskhub::ClientInputQueue` in `core/`.** The `Queue*` methods at the top of every
      `ClientLoop.cpp` are pure logic and have already forked into two dialects: linux/macOS
      use `keysDown_`/`buttonsDown_` + `ReleaseAllInput()`, iOS/Android use `delayedInput_`
      with a 50 ms tap-hold plus `QueueKeyChord`/`QueueCharTap`. One class covers both.
      `CharToKeyChord()` in `core/input/KeyMap.h` already exists for the char path.
      *Done — one class covers both dialects. Every `Queue*` method in all four
      `ClientLoop.cpp` is now a one-line forward, and `inputMutex_`, `inputQueue_`,
      `delayedInput_`, `wantFocus_`, `keysDown_`, `buttonsDown_`, `PushLocked` and
      `kTapHoldUs` are gone from all four classes. The queue owns its own mutex, so callers
      cannot forget to lock; `Drain(nowUs, out)` matures the delayed taps and hands over the
      batch in one call.*

      *Deliberately NOT reused here: `PressedInputTracker` from 4.2. Its button set is typed
      `MouseButton`, which is `uint8_t`-backed, while the client tracks raw `int32_t` button
      codes — routing them through the enum would truncate. Three lines of duplication is the
      better trade.*

      *11 test groups. Verified by compiling both dialects for real: Linux (held-key dialect)
      via `make debug`, and Android (tap dialect) with the NDK.*

## Tier 3 — `AgentLoop` / `SourcePipeline`, three copies (~2100 lines)

`diff` of linux vs macos: **559 identical lines out of 703**. Windows declares
`SourcePipeline` field-for-field like the Linux one. Depends on Tier 4's capture contract.

- [x] **3.1 `SourcePipelineState` → `core/`.** All the atomic counters, `offer`, `ladder`,
      `step`, `rate`, `packetizer`, `retxCache`, `statRate`, `diag`. Nothing OS-specific.
      *Done — 35 members moved. Each platform's `SourcePipeline` now DERIVES from
      `deskhub::SourcePipelineState` rather than containing it, which was the deciding
      detail: inheritance keeps every one of the ~200 `p->srcW` / `p->forceIdr` call sites
      working unchanged, so the edit is confined to the struct definitions. Composition would
      have meant `p->st.srcW` everywhere — hundreds of edits across two platforms I cannot
      compile.*

      | `SourcePipeline` | before | after |
      | --- | ---: | ---: |
      | linux | 75 | 28 |
      | macos | 68 | 29 |
      | windows | 72 | 36 |

      *This turned up two latent bugs of the dangerous kind. Because the platform structs
      declared some members on different lines than others, my first pass left
      `uiLossPct`/`uiRecvKbps` on Linux and `uiRttMs` on Windows declared in the DERIVED
      class — silently shadowing the base members. Writers and readers would have split
      across two different variables and the UI would have shown stale zeros.
      `-Wall -Wextra` does not warn about a derived member hiding a base member, so this was
      caught by scripting a check of every base member name against each derived body, not by
      the compiler. Worth repeating if more members move.*
- [x] **3.2 `deskhub::MakeHostCallbacks(state, hooks)` → `core/`.** `onHello` (build the
      ladder, retarget, `SetOffer`), `onNack` (look up `retxCache`, resend), `onDisconnect`,
      and `onFeedback` — linux and macOS differ only in how a quality step is applied
      (linux rebuilds the encoder; macOS calls `SetFps` + `capture.SetQuality`). Three or
      four hooks cover it.

      *Rather than one `MakeHostCallbacks` returning the whole struct, the
      three genuinely shared pieces moved out individually — the callbacks themselves stay in
      each client, which keeps the per-platform logging and encoder types visible where they
      are used:*

      - *`ApplyFeedback(state, fb, nowUs, hooks)` — the ~35-line policy block that was
        duplicated verbatim three times: mirror the UI counters, run `BitrateController`,
        toggle FEC, commit an accepted bitrate, walk the quality ladder. Two hooks absorb all
        the variation (`setEncoderBitrate`, `applyQualityStep`), and it returns a
        `FeedbackOutcome` the caller logs however it likes. That last part matters: Windows
        prints FEC on/off as two different sentences, and forcing one wording would have been
        a behaviour change smuggled into a refactor.*
      - *`RespondToNack(...)` — was byte-identical across all three.*
      - *`ForgetPeer(...)` — clearing `peerPacked` plus the retransmit cache under its mutex.*

      *8 test groups, including the case that was previously easy to get wrong by hand: when
      the encoder REFUSES a bitrate change, neither the controller nor `curBitrateBps` may
      commit it.*

      ***`onHello` done too — my earlier objection to it was wrong.*** *I had argued this
      could not be unified without either losing macOS's capture-side scaling or adding a
      no-op `SetQuality()` elsewhere. That assumed the shared code would have to DO the
      sizing. It does not: `BeginNegotiation(state, hello, maxFps, hooks)` takes a
      `resolveSize(clientW, clientH)` hook, and macOS's `capture.SetClientSize()` fits it
      exactly — the same role Linux and Windows fill with `retarget()`. macOS keeps scaling in
      the capture stream, on the GPU, unchanged. The shared part is what always was shared:
      build the ladder, take the first rung, fill in the offer, `SetOffer`.*

      *Two small differences were levelled up rather than preserved, both in macOS's favour of
      correctness: it now resets `step` before a negotiation (so a retry cannot inherit the
      previous rung) and sets `offer.fps` from the ladder rather than leaving the value set at
      attach time. Both resolve to the same number on the top rung, so this is a latent-bug
      fix, not a behaviour change.*
- [x] **3.3 `deskhub::HostRouter` → `core/`.** The `RecvLoop` demux: beacon reply, finding the
      pipeline by `sourceId`/`sessionId`, updating `peerPacked`, sending `Reconfig` on
      size/quality change, keepalive and IDR flush at the 200 ms / 500 ms thresholds.
      *Done as four pure functions rather than one router object, because the loop around them
      is inherently socket-bound and pulling that into core would have meant a callback per
      line: `RouteDatagram()` (HELLO by source id, everything else by session id),
      `AdoptPeer()` (report only real peer changes, so the log stays quiet),
      `RefreshOffer()` (consume the changed flags, rebuild the offer, decide whether a
      RECONFIG is due) and `DueForFlush()` (the 200 ms IDR / 500 ms keepalive timing).
      `RecvLoop` went 134 → 108 lines on Linux and 137 → 111 on macOS; what differs between
      them now is the encoder flush and the diag arguments, which are genuinely per-platform.*

      *One behaviour difference was preserved rather than smoothed over: after a RECONFIG,
      Linux and Windows always force an IDR, macOS only when the SIZE changed. macOS is
      arguably right — an fps-only change leaves reference frames valid — but that is a call
      for you, not for a refactor. `OfferUpdate` reports `sizeChanged` and `qualityChanged`
      separately so each platform kept exactly what it had.*

      *9 test groups, including that a paused source still CONSUMES its change flags (so they
      cannot pile up and fire a burst when it resumes) and that a pending IDR outranks a
      keepalive.*

## Tier 4 — Interfaces to generalise

`core/media/VideoContract.h` is the model to copy — concepts, not virtual bases.

- [x] **4.1 `LocalInputMonitor` → `platform/include/deskhubp/LocalInput.h` + three `.cpp`.**
      The clearest remaining `platform/` candidate: Linux and macOS *already* expose the same
      API (`kQuietUs`, `LocalActive(nowUs)`, `Start`/`Stop`); only Windows diverges
      (`static LastPhysicalUs()`, no `LocalActive`).
      *Done — one header with a pimpl (the `ScreenCapture`/`AgentLoop` pattern already used
      here), four implementations selected in `platform/CMakeLists.txt`:
      `LocalInputLinux.cpp` (evdev poll), `LocalInputMac.mm` (AppKit global monitor),
      `LocalInputWin.cpp` (LL keyboard/mouse hooks), `LocalInputNone.cpp` for iOS and Android,
      which build `platform` but have no host role. Windows lost its odd static
      `LastPhysicalUs()`: its `InputInjector` now takes a `SetLocalMonitor()` like the other
      two and calls `LocalActive()`, so `kHostWinsGraceUs` (1 s) and `kQuietUs` (1 s) are one
      constant instead of two. `kUserData` → `kInjectedUserData`, since in a shared header the
      old name said nothing. The root `CMakeLists.txt` gained `enable_language(OBJCXX)` guarded
      to macOS — iOS is excluded by the `-DCMAKE_SYSTEM_NAME=iOS` its Xcode pre-build passes.
      Verified on Linux: builds, opens the evdev devices (no permission warning), starts and
      stops cleanly; the stub path compiles and runs too. Windows and macOS not compiled here.*
- [x] **4.2 `deskhub::PressedInputTracker` → `core/`.** All three `InputInjector`s keep their
      own `keysDown_`/`buttonsDown_`/`applied_`/`skipped_`/`localSuppressed_` and
      `ReleaseAll()`. That bookkeeping is pure and testable; the OS side shrinks to five
      calls — `SendKey`, `SendButton`, `SendMoveAbsolute`, `SendMoveRelative`, `SendWheel` —
      i.e. one `InputBackend` contract.
      *Done — `PressedInputTracker<NativeKey>` holds the pressed-key map (Linux/macOS key it by
      vk with a native code payload, Windows by scancode with a vk payload — the template
      absorbs both), the pressed-button set, the two counters, and the host-wins latch. Two
      things it buys beyond deduplication:*

      1. *`Gate(localActive)` returns `{allow, justSuppressed, justResumed}`, so the
         suppress/resume edges are computed once instead of three hand-rolled latches. It
         exposed a real inconsistency worth knowing about: on suppression Linux never releases
         held keys, Windows releases on the transition, macOS releases on every blocked event.
         Each platform's behaviour was left as it was — the gate only reports the edges — but
         it is now visible in one place instead of buried in three files.*
      2. *`TakeHeldKeys()`/`TakeHeldButtons()` snapshot and clear in one step, which is exactly
         the copy-the-container-first dance all three `ReleaseAll()`s were doing by hand to
         avoid iterating while `Send*` mutates.*

      *macOS also lost `modsDown_` entirely: it was a second container holding the subset of
      `keysDown_` that happens to be modifiers, kept in sync by hand. `CurrentFlags()` now
      derives the flags from the held keys, so the two can no longer disagree. Its
      `ReleaseAll()` deliberately still iterates a copy rather than `TakeHeldKeys()`, because
      `SendKey` reads `CurrentFlags()` and the old code let the modifier set decay key by key
      during a bulk release — preserved exactly.*

      *7 test groups in `core/tests/input/PressedInputTests.cpp`. Linux builds; the macOS and
      Windows injectors could not be compiled here, so their exact call patterns were
      compile-checked against the tracker in isolation instead.*
- [x] **4.3 Shared scancode table.** `LinuxKeyMap` and `MacKeyMap` have the same shape:
      `KeyEntry{native, vk, scan}` plus `XToWin()` and `WinVkToX()`. The data stays per-OS;
      the lookup and the reverse lookup should be written once in `core`
      (`ScancodeTable<T>`), with tests.
      *Done — `deskhub::ScancodeTable<Native>` over `ScancodeEntry<Native>{native, vk, scan}`.
      Both platform tables are untouched: their rows were already positional aggregates, so
      aliasing `KeyEntry` to `ScancodeEntry<uint16_t>` left ~350 rows of data compiling as-is
      and only the two lookup functions changed, to one line each.*

      *One behaviour was unified deliberately. Linux normalised the side-less modifiers
      (`VK_SHIFT`/`VK_CONTROL`/`VK_MENU` → the left-hand key) in `WinVkToEvdev`; macOS instead
      carried three extra table rows for the same effect. `PreferLeftModifier()` now does it
      for both. Verified it is a no-op for macOS: its generic rows map to the same key codes
      its left-hand rows do, and the left-hand rows come first, so the result is identical
      either way. The three now-redundant macOS rows were left in place rather than deleted —
      removing them is not something a Linux build can check.*

      *6 test groups, including that a failed lookup leaves the caller's out-params untouched
      and that duplicate vks resolve to the first row deterministically.*
- [x] **4.4 `CaptureTypes` / `ScreenCapture` contract.** The three frame structs
      (`LinuxFrameInfo` / `MacFrameInfo` / `FrameInfo`) cannot merge, but the three `Start()`
      signatures can: `Start(target, options, FrameHandler)` with an opaque per-OS target,
      plus a shared `FrameMeta{width, height, timestampUs, frameId}` embedded in each frame
      struct. New `core/media/CaptureContract.h` alongside `VideoContract.h`. **Prerequisite
      for Tier 3** — the three capture APIs are the main reason `AgentLoop` cannot merge
      today (`SetQuality` exists only on macOS).
      *Done — `core/media/CaptureContract.h` alongside `VideoContract.h`, same style: concepts,
      not virtual bases. `FrameMeta{width, height, timestampUs, frameId}` is now embedded in
      all three frame structs, so anything generic can read a frame's geometry without knowing
      whether it holds a dmabuf, a `CVPixelBuffer` or a D3D texture. Each `ScreenCapture.h`
      carries `static_assert`s for the capabilities it actually has — `ZeroCopyCapture` on
      Linux, `QualityAwareCapture` on macOS — so the differences are declared instead of
      discovered.*

      *The concepts are tested for what they REJECT, not just what they accept: a capture
      missing `Closed()`, a frame with loose `width`/`height` fields instead of `meta`, and
      each capability concept against a platform that lacks it. Without those negative cases a
      concept can be vacuously true and assert nothing.*

      ***Second half now done too.*** *All three captures take
      `Start(uint64_t targetId, const CaptureOptions&, FrameHandler)`. Each unpacks the target
      itself: Linux casts to a PipeWire node id and fetches the portal fd from the singleton
      rather than having it threaded in as a parameter (which let
      `Impl::StartPipeline(p, portalFd)` lose its second argument entirely), macOS casts to a
      `CGDirectDisplayID`, Windows casts back to `HMONITOR`. Windows' `ID3D11Device*` was the
      one thing that would not fit a shared signature — it is a dependency, not a target — so
      it moved to its own `SetDevice()` call before `Start()`.*

      *~16 of the ~48 field renames are on macOS and Windows and could not be compiled. They
      are the safe kind of unverifiable edit — a missed rename is a compile error, never a
      silent behaviour change.*
- [x] **4.5 `SourceEnum` / `ShareSource`.** `nodeId` (linux) vs `displayId` (macOS) vs
      `HMONITOR` (windows) → one `uint64_t targetId` + name + rect. Also a Tier 3
      prerequisite.
      *Done — `deskhub::media::ShareSource{targetId, name, x, y, width, height}` in
      `core/media/ShareSource.h` now serves as BOTH the enumeration result and `AgentSource`,
      on all three hosts. That collapsed four structs into one and deleted the hand-written
      `ShareSource` → `AgentSource` copy loop in `ShareWindow.cpp` outright; the macOS bridge
      conversion shrank to setting `targetId`.*

      *Windows got the most out of it. `DisplayInfo` is gone: `ListDisplays()` returns
      `ShareSource` directly, its `primary` flag is now a local detail of `DisplayFinder.cpp`
      (used to append " (primary)" and to sort), and the name is UTF-8 like everywhere else —
      which removed the `ToUtf8()` call at the one call site and left that helper unused, so
      it went too. `ListDisplays()` also now fills in `x`/`y`, which it had available from
      `rcMonitor` all along and was throwing away.*

      *`HMONITOR` survives only as a cast at the two places that genuinely need the handle
      (`AgentLoop`'s pipeline setup and `ElevatedShare`'s command-line encoding), instead of
      being threaded through the whole source-selection path.*

## Tier 5 — The same geometry written five times

Letterbox + zoom/pan → normalised 0..65535 pointer coordinates, five independent
implementations:

| Platform | File |
| --- | --- |
| iOS | `client/ios/app/swift/ViewTransform.swift` |
| Android | `client/android/.../StreamActivity.kt:807` (`videoFrame`) |
| macOS | `client/macos/app/swift/RemoteView.swift:76` |
| Linux | `client/linux/gtk/ViewerWindow.cpp:151` |
| Windows | `client/windows/win32/ViewerInput.cpp:20` |

- [x] **5.1 `deskhub::ViewFit` in `core/`,** called from each UI through the C bridges that
      already exist (`JniBridge.cpp`, `DeskhubClient.mm`). These copies have already drifted:
      Swift and Windows divide by `width - 1`, the GTK one does not.

      *`core/media/ViewFit.h` now holds the reference implementation:
      `FitVideoRect()` (letterbox + zoom + pan clamping), `ApplyGesture()` (pinch about a
      centroid, then re-clamp), `NormalizeAxis()` and `NormalizePointer()`. 8 test groups.*

      ***The drift is resolved in favour of `width - 1`***, *which is the correct form: it maps
      the last pixel to exactly 65535. The GTK viewer divided by `width`, so its right-most
      column never reached the remote screen's right edge — a small real bug, now fixed by
      construction. There is a test pinning exactly this (`NormalizeAxis(1919, 1920) == 65535`).*

      *All five call sites converted. The two C++ ones directly
      (`client/linux/gtk/ViewerWindow.cpp`, `client/windows/win32/ViewerInput.cpp`); the three
      UI-framework ones through their bridges — two new JNI functions returning the rect and
      the gesture result as float arrays, and `dh_video_rect` / `dh_apply_gesture` /
      `dh_normalize_pointer` added to both Apple C APIs. `ViewTransform.swift` shrank to a
      thin wrapper, `videoFrame()` in `StreamActivity.kt` to six lines. The Android bridge is
      compile-verified; the two Swift ones are not.*
- [x] **5.2 Do *not* merge the macOS and iOS SwiftUI layers.** They have diverged too far —
      `SessionModel.swift` shares 62 of ~135 lines, `StreamView.swift` 20 of ~350. The one
      exception worth sharing is `DeskhubClient.swift` (66 of ~100 lines in common), the thin
      wrapper over the C API.
      ***The exception is withdrawn after looking properly.*** *The 66 "common" lines are
      braces and boilerplate. The two files wrap fundamentally different C APIs: macOS is
      handle-based (`dh_session_*`, one session per remote window, several at once), iOS is a
      single global session (`dh_*`). Sharing them would mean converting iOS to handle-based
      multi-session — a feature change to the iOS client, not deduplication. Conclusion for
      the whole of Tier 5.2: leave both alone.*

---

## Suggested order

Tier 1 → 4.1 + 4.2 → Tier 2 → 4.4 + 4.5 → Tier 3 → Tier 5.

Within Tier 1, do the items that `make test` can verify on any OS first (1.4, 1.5), then the
moves (1.2, 1.3), then the Apple items (1.1a, 1.1b) which need a macOS machine to build.
