# 07 — Input Pipeline

How keyboard/mouse/touch input travels from a client to the machine being controlled. The wire
format lives in 04-protocol.md; this document covers the event model, the reliability strategy,
the key-code space, per-platform capture, host-side injection, and the safety mechanisms.

```
capture (per-platform UI) → InputSender (core) → UDP ~~~> InputReceiver (core) → InputInjector (per-OS) → OS
```

## 1. Event model

`deskhub::InputEvent` (core/include/deskhub/protocol/Wire.h) is the single event type, 19 bytes on the
wire (`kInputEventSize`), with four kinds (`InputType`):

| Type | `a` | `b` | `state` / `absolute` |
|---|---|---|---|
| `Key = 1` | Windows virtual-key code (VK) | scancode, bit 8 (`kScanExtended = 0x100`) = E0 extended flag | `state`: 1 = down, 0 = up |
| `MouseMove = 2` | x (or dx) | y (or dy) | `absolute = 1`: normalized 0..65535 in the captured frame; `absolute = 0`: raw pixel delta |
| `MouseButton = 3` | `MouseButton` enum (Left=1, Right=2, Middle=3, X1=4, X2=5) | 0 | `state`: 1 = down, 0 = up |
| `MouseWheel = 4` | 0 | delta, multiples of 120 (Windows `WHEEL_DELTA`) | ignored |

Each event also carries `timestampUs`. An `INPUT_EVENT` datagram (`MsgType::InputEvent = 0x20`) is
`firstSeq (u32) + count (u8)` followed by up to `kMaxInputEvents = 62` events; the seq of the i-th
event in a packet is `firstSeq + i`. **seq is per event, not per packet** — that is what lets the
receiver deduplicate deliberately repeated events.

### Reliability: redundancy + tail replay (InputSender, client side)

The input channel runs over plain UDP with no ACKs. Losing a *key-up* event is the worst failure in
the system (a game character runs forever), so `InputSender`
(core/include/deskhub/input/InputSender.h, core/src/input/InputSender.cpp) compensates at the
sender:

- `Queue()` assigns a monotonically increasing seq and appends to a history deque; nothing is sent.
- `Flush(nowUs, send)` — called every loop iteration of the client's Recv/Net thread — packs new
  events into batches of at most `kInputBatchMax = 24`, and **prefixes each datagram with the last
  `kInputRedundancy = 8` already-sent events**.
- When no new events remain, the tail of the history is re-sent `kInputRepeatCount = 2` more
  times, spaced `kInputRepeatIntervalUs = 25'000` µs apart.

Net effect: every event crosses the wire about 3 times within ~50 ms; isolated packet loss is
invisible. History is trimmed at `kHistoryMax = kInputRedundancy + kInputBatchMax * 2` entries
while preserving seq continuity. `Wire.h::IsStateEvent()` documents which events are
state-changing (Key, MouseButton) and thus benefit from the repeats.

### Dedupe and ordering (InputReceiver, host side)

`InputReceiver` (core/include/deskhub/input/InputReceiver.h/.cpp) holds a single piece of state,
`lastAppliedSeq_` (`int64_t`, −1 = nothing yet). Rule: `seq <= lastApplied` → drop (counted in
`stats().duplicates`); otherwise apply and advance. Consequences:

- repeated events (from the redundancy policy) are dropped exactly once each — a press is one press;
- late/reordered packets containing only old seqs are dropped entirely — stale input is never
  applied ("rewinding" the cursor is worse than dropping);
- seq gaps that no repeat filled are counted in `stats().lost` for observability only. There is no
  retransmission request and no reorder buffer, by design: an input event that arrives hundreds of
  milliseconds late is worse than one that never arrives.

`HandlePacket()` returns true for any valid packet even if every event was a duplicate — the
caller (`HostSession`) uses that as liveness evidence for the session timeout. Both ends have
`Reset()` for a new session (sender restarts at seq 0, receiver forgets `lastAppliedSeq_`).

All of this is pure C++20, single-threaded by contract, and covered offline by
core/tests/input/InputTests.cpp (wire round-trip, dedupe under 1/3 packet loss, reorder rejection,
loss counting beyond redundancy, batch splitting, history trim seq continuity, reset).

## 2. Key code space

The protocol speaks **Windows**: `InputEvent::a` is a Windows virtual-key code, `InputEvent::b` is
a PC scan code set 1 with bit 8 as the E0 flag. Scancodes matter because DirectInput/Raw Input
game engines read scancodes only — a `SendInput` with just `wVk` is invisible to them.

Three sources of key codes exist:

1. **Windows client** (client/windows/win32/ViewerInput.cpp): Raw Input (`WM_INPUT`) provides the
   real scancode (`RAWKEYBOARD.MakeCode`, with `RI_KEY_E0` mapped to the `kScanExtended` bit) —
   sent as-is via `dh_client_key`.
2. **macOS client** (client/macos/app/cpp/input/MacKeyMap.h/.cpp): a single ~110-row table maps
   Carbon virtual keycodes ↔ (VK, scancode) covering letters, digits, OEM punctuation, modifiers
   (left/right variants first so generic `VK_SHIFT`/`VK_CONTROL`/`VK_MENU` resolve to the left
   key), navigation cluster (all E0), F1..F20 and the numeric keypad. `MacToWin()` is the client
   direction; `WinVkToMac()` serves the macOS *agent* when it receives input; `ModifierOf()`
   classifies VKs into Shift/Control/Option/Command/CapsLock for building `CGEventFlags`. Keys not
   in the table (media, Fn) are silently skipped. US layout is an accepted limitation.
3. **Mobile virtual keyboards** (core/include/deskhub/input/KeyMap.h): soft keyboards deliver
   *characters*, not keys. `CharToKeyChord(codepoint)` maps a printable ASCII character (plus
   `\b`, `\t`, `\n`, `\r`) to `{vk, shift}` assuming a **US layout on the host**, and returns
   `nullopt` for anything else (non-ASCII is dropped). These events are sent with **scan = 0**:
   `InputInjector` on Windows detects a zero scancode and looks it up itself via
   `MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)` against the *host's* layout — the correct place to
   resolve it, and still scancode injection, so games see the key.

Mobile hotkey bars (see §6) hardcode VK + US scancode pairs directly, e.g. Esc `(0x1B, 0x01)`,
arrows with the E0 bit (`0x148`, `0x150`, `0x14B`, `0x14D`).

## 3. Mouse model

**Absolute mode** — `MouseMove` with `absolute = 1` and coordinates normalized to 0..65535 within
the frame the client sees (the captured region, not the screen). The 0..65535 scale makes the
mapping independent of how the client scales its preview. Endpoints are exact: clients normalize
with `pos / extent * 65535` and hosts denormalize with `n * (w−1) / 65535`, so 65535 lands on the
last pixel.

**Relative mode (pointer lock, F9)** — `MouseMove` with `absolute = 0` carrying raw deltas.
This exists because games ignore absolute coordinates; they read raw motion deltas and apply their
own sensitivity. Per-platform:

- *Windows client* (client/windows/win32/ViewerInput.cpp): F9 (`kToggleRelativeKey`) toggles
  relative mode. While locked the cursor is hidden and confined to the viewer window with
  `ClipCursor`, and Raw Input mouse deltas are sent via `dh_client_mouse_move_rel`. **F9 is
  consumed locally and never forwarded** — it is the only exit from lock mode. Locking is
  refused for view-only sessions.
- *macOS client* (client/macos/app/swift/RemoteView.swift): same two modes; lock uses
  `CGAssociateMouseAndMouseCursorPosition(0)` to detach the cursor from the physical mouse and
  sends `NSEvent.deltaX/deltaY` (raw device deltas, no pointer acceleration). F9 (keyCode 0x65)
  toggles lock locally, mirroring the Windows client.
- *Mobile*: relative mode is plumbed end-to-end (`dh_mouse_move_rel` on iOS,
  `nativeMouseMoveRel` on Android → `ClientLoop::QueueMouseMoveRel`) but **no mobile UI calls it
  yet** — the Android declaration says so explicitly. Mobile currently drives absolute mode only.

**Wheel** — delta in multiples of 120. Windows injects it verbatim (`MOUSEEVENTF_WHEEL`); the
macOS agent converts 120 → 3 lines (`kCGScrollEventUnitLine`), matching the Windows default so
remote scrolling feels native. Mobile clients do not send wheel events.

### Mobile trackpad model

Both touch clients implement a laptop-style virtual trackpad rather than direct touch — the finger
is decoupled from an always-visible client-drawn cursor, so small targets stay hittable and the
finger never occludes them. Implemented twice, identically: `TrackpadOverlay` in
client/android/app/src/main/java/com/deskhub/app/StreamActivity.kt (Compose gesture detectors)
and `TouchCaptureUIView` in client/ios/app/swift/TouchInputView.swift (UIKit gesture recognizers):

- **drag** moves the cursor by delta;
- **single tap** = left click *at the cursor* (waits out the double-tap window first);
- **double tap** = right click at the cursor;
- **long-press then drag** = hold left button while moving (window drag / text selection),
  released on lift/cancel.

The overlay covers the whole display area including letterbox bars, but the cursor is clamped to
the actual video rect (aspect-fit, centered, computed from the video aspect ratio), and
coordinates are normalized 0..65535 against that rect. Before every click the current cursor
position is re-sent, so the click lands where the cursor is drawn even if someone at the host
moved the real pointer in between. The initial cursor placement (center of frame) is display-only
and deliberately not sent.

## 4. Host-side injection

### Windows (client/windows/cpp/input/InputInjector.h/.cpp)

`InputInjector::Apply()` runs on the agent's Recv thread and injects via `SendInput`:

- **Keys**: `KEYEVENTF_SCANCODE` (+ `KEYEVENTF_EXTENDEDKEY` when the E0 bit is set — without it
  arrow keys become numpad digits). If the client sent scan = 0 (mobile virtual keyboard), the
  scancode is resolved on the host with `MapVirtualKeyW`; only if that also fails does it fall
  back to `wVk`. Scancode-first injection is the point of the whole feature: it is what makes
  DirectInput/Raw Input games see remote keys.
- **Absolute moves**: two distinct 0..65535 scales are involved. The incoming coordinates are
  relative to the captured region — always the shared monitor's rect (`GetMonitorInfoW`), since
  displays are the only source kind (the window path via `DWMWA_EXTENDED_FRAME_BOUNDS` was
  removed 2026-07-27). They are
  converted to screen pixels, then re-normalized to the **virtual desktop** for
  `MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK` (`ScreenToVirtualDesk`) — mandatory on
  multi-monitor machines.
- **Relative moves**: `MOUSEEVENTF_MOVE` without `ABSOLUTE` — a raw delta, which is what games
  read.
- **No foreground handling**: `SendInput` targets whatever window is foreground, which is by
  definition on some display — and displays are all that is shared. The old
  `ForceForeground`/`FocusTarget`/`TargetHasFocus` machinery (raising the shared window via the
  `AttachThreadInput` trick) was removed 2026-07-27 along with window sharing; `SET_FOCUS(true)`
  now requires no host action (04-protocol.md §4.13).
- **UIPI/elevation caveat**: `SendInput` from a normal-privilege agent cannot reach elevated
  windows, and the `WH_*_LL` hooks of LocalInputMonitor likewise cannot see input going into them
  (noted in LocalInputMonitor.h). Both halves degrade together; see 02-agent.md for the elevated
  share path.

### macOS (client/macos/app/cpp/input/InputInjector.mm)

Injection uses Quartz Event Services: events are posted to `kCGHIDEventTap` (the earliest point in
the event pipeline, so low-level HID readers and fullscreen games see them), from a
`kCGEventSourceStateHIDSystemState` source (so injected modifiers blend with physically held ones).
Details:

- **Accessibility permission is required.** `Init()` warns but does not fail without it — macOS
  silently drops injected events until the user grants it (effective immediately, no restart);
  sharing continues view-only. See 14-macos-app.md.
- VK → macOS keycode via `mackeys::WinVkToMac`; unmappable keys are skipped. Modifier state is
  tracked in `modsDown_` and stamped as `CGEventFlags` on *every* event (required — without flags
  Shift+A produces 'a'); the bookkeeping happens before building the event so the Shift-down event
  itself carries the Shift flag.
- Absolute moves map 0..65535 into the shared display's bounds (`Init(displayId)`; the
  `CGWindowListCopyWindowInfo` window-bounds path was removed 2026-07-27 with window sharing). Relative moves advance the current cursor
  position, clamp to the union of all screens, and also set `kCGMouseEventDeltaX/Y` — the raw
  delta fields FPS games read. Moves while buttons are held become proper drag event types.
- Buttons synthesize `kCGMouseEventClickState` (double-click) using a 500 ms /
  4 pt window (`kDoubleClickUs`, `kDoubleClickSlopPt`). X1/X2 map to Quartz buttons 3/4.
- Every posted event is stamped with `LocalInputMonitor::kUserData`
  (`0x4445534B'48554200`, "DESKHUB\0") in `kCGEventSourceUserData` so the local-input monitor can
  filter out our own injections.
- No foreground gate: the old `TargetHasFocus()`/`FocusTarget()` pair (PID comparison against
  `NSWorkspace.frontmostApplication`, async app activation) was removed 2026-07-27 — with a whole
  display as the source there is no owning app to gate on.

## 5. Safety mechanisms

Both injectors implement the same two-layer policy (documented at the top of each
InputInjector.h). A third layer — a *foreground gate* that injected only while the shared window
was frontmost — was removed 2026-07-27 together with window sharing: with whole displays as the
only source kind there is no "other application outside the share" to protect, so the gate had
nothing left to guard. `skipped_` now counts only events yielded under "host wins".

1. **Stuck-key release (`ReleaseAll`).** The injector remembers every currently-down key and
   button — on Windows keyed by scancode+E0 (`keysDown_`; two keys can share a VK, e.g. left/right
   Ctrl, but never a scancode), on macOS by VK. `ReleaseAll()` synthesizes the missing up events
   and is called on client disconnect (BYE/timeout — `HostSession` fires `onDisconnect`, wired in
   client/windows/cpp/AgentLoop.cpp and the macOS AgentLoop.cpp), on `SetEnabled(false)`, on
   `SET_FOCUS(false)` (the client's preview lost focus or it switched away from this source), on
   entry to the "host wins" suppressed state, and on source/agent shutdown.
2. **"Host wins" (LocalInputMonitor).** When the person physically at the host machine touches
   the real mouse or keyboard, remote input yields for ~1 s (`kHostWinsGraceUs` /
   `kQuietUs = 1'000'000` µs) and `ReleaseAll()` runs on entry to the suppressed state. This
   prevents cursor tug-of-war and cross-contaminated modifiers (host holds real Ctrl + remote
   types S = Ctrl+S). Detection:
   - *Windows* (client/windows/cpp/input/LocalInputMonitor.h/.cpp): global `WH_KEYBOARD_LL` +
     `WH_MOUSE_LL` hooks on a dedicated message-pump thread, recording the timestamp of the last
     event **without** `LLKHF_INJECTED`/`LLMHF_INJECTED` — those flags mark our own `SendInput`
     traffic, and filtering them is what prevents a self-suppression feedback loop. If the hooks
     fail to install, `LastPhysicalUs()` stays 0 and the mechanism silently disables itself.
   - *macOS* (client/macos/app/cpp/input/LocalInputMonitor.h/.mm): an `NSEvent` global monitor on
     the main run loop (listen-only, cannot swallow events), filtering events stamped with
     `kUserData`.

Additional guards along the path:

- The client stops queueing input entirely when the host declared `inputAccepted = 0` in HELLO_ACK
  (`ClientSession::QueueInput` in core/src/session/ClientSession.cpp) — mouse moves would
  otherwise fight video for bandwidth for nothing. UIs also gate at their layer
  (`NativeClient.viewOnly` on Android, `SessionModel` checks on iOS, hidden lock button on
  Windows).
- On session teardown the host resets `InputReceiver` and calls `ReleaseAll`, so a reconnecting
  client restarting at seq 0 is handled cleanly.
- Mobile tap events split press/release across time: the release of a tap is scheduled
  `kTapHoldUs = 50'000` µs after the press (`ClientLoop::QueueKeyTap`, `delayedInput_`), because a
  0 ms down/up pair is invisible to games polling the keyboard per frame. The delayed release is
  drained by the Net loop, and the redundancy layer protects it — a lost release packet is
  re-sent, so this does not reintroduce stuck keys.

**Not implemented:** an F10 "pause input forwarding" toggle is mentioned in README.md but does not
exist anywhere in the current code — no client or agent handles F10. The only runtime input
toggles are the host's allow-input setting (wired to `InputInjector::SetEnabled`) and the
mechanisms above.

## 6. Client-side capture per platform

- **Windows** (client/windows/win32/ViewerInput.cpp → C API in
  client/windows/cpp/DeskhubApi.h, ClientApi.cpp): the viewer window registers Raw Input for
  mouse + keyboard (no `RIDEV_NOLEGACY`, so ordinary messages still work; no `RIDEV_INPUTSINK`,
  so alt-tabbing away types into the local machine as normal). Keys come from `WM_INPUT`
  (`RAWKEYBOARD` VK + scancode + E0), absolute mouse from `WM_MOUSEMOVE` normalized against the
  client rect, relative deltas from Raw Input while locked; while input forwarding is on, nearly
  all key messages (including ESC) are swallowed so they reach the remote machine — F9 is the
  local escape hatch (§3). The
  `dh_client_mouse_move[_rel]/mouse_button/wheel/key` C functions build `InputEvent`s into a
  mutex-guarded queue that the Recv thread drains into `ClientSession::QueueInput`; the session's
  `Tick` calls `InputSender::Flush`. See 03-client.md.
- **macOS** (client/macos/app/swift/RemoteView.swift): an `NSView` first responder with a tracking
  area receives `keyDown/keyUp` (translated via `MacToWin`), `flagsChanged` (modifiers produce no
  keyDown/keyUp; the previous flag mask is diffed to synthesize down/up), mouse moves/drags,
  three buttons, and `scrollWheel` (converted to ±120 steps). F9 toggles lock locally. See
  14-macos-app.md.
- **Android** (StreamActivity.kt, KeyInputView.kt, NativeClient.kt →
  client/android/app/src/main/cpp/JniBridge.cpp → ClientLoop.cpp): the trackpad overlay (§3)
  supplies mouse input. Typing goes through `KeyInputView`, an invisible focusable 1 dp view whose
  `InputConnection` (VISIBLE_PASSWORD + NO_SUGGESTIONS, so IMEs commit raw keystrokes instead of
  composed text) catches both IME paths — `commitText`/`deleteSurroundingText` from soft
  keyboards and `onKeyDown` from physical/Bluetooth keyboards — and forwards codepoints to
  `NativeClient.charTap` → `ClientLoop::QueueCharTap` → `CharToKeyChord`. A horizontally
  scrolling hotkey bar provides keys the soft keyboard lacks: Esc, Tab, Enter, arrows, Del,
  Ctrl+C, Ctrl+V (`kHotkeys` in StreamActivity.kt; single keys via `keyTap`, combos via
  `keyChord`, which enqueues mod-down, key-down atomically and schedules key-up before mod-up).
  All input funnels are gated by `NativeClient.viewOnly`. See 08-android-client.md.
- **iOS** (TouchInputView.swift, KeyInputView.swift, StreamView.swift → DeskhubClient.swift →
  client/ios/app/cpp/DeskhubClient.mm → ClientLoop.cpp): structurally identical to Android — the
  trackpad overlay, a `UIKeyInput` view (`asciiCapable`, autocorrect off) whose
  `insertText`/`deleteBackward` feed `charTap`, and the same `kHotkeys` list rendered as a
  scrolling pill row (plus a Done accessory button to dismiss the keyboard). The iOS and Android
  native loops share the queue design: UI threads push into `inputQueue_` under `inputMutex_`, the
  Net thread drains it (plus due entries of `delayedInput_`) into `ClientSession::QueueInput`
  every iteration. Any queued input also sets `wantFocus_`, which makes the session send
  `SET_FOCUS` — the host no longer raises anything on `true` (that behavior left with window
  sharing, 2026-07-27); only the `false` edge matters, releasing held keys. See 12-ios-client.md.

Mobile hotkey lists still exclude Alt+Tab and the Win key — originally because they would move
focus off the shared window and trip the (since-removed) foreground gate; today simply because
app switching on the host is rarely what a trackpad user wants from a hotkey bar.

## 7. Testing

- `core/tests/input/InputTests.cpp` covers the transport-independent half offline (`make test`):
  wire round-trip including negative relative coordinates, exactly-once delivery under 1/3 packet
  loss, reorder rejection, loss accounting, batch splitting past `kInputBatchMax`, history-trim
  seq continuity, `Reset` on both ends, and the full `CharToKeyChord` table (every printable ASCII
  character must map).
- `InputInjector::SelfTest` (`--injecttest`) was removed 2026-07-27 with the foreground/window
  machinery it depended on; the injection half is now exercised only through real sessions.
