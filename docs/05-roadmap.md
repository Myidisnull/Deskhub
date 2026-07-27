# 05 — Implementation Roadmap

The roadmap has **two dimensions**:

1. **Depth (Phase 0–Phase 6)** — build the complete pipeline on **Windows as the reference
   implementation**, ordered by **decreasing risk**: tackle the most failure-prone parts first
   (encode in Phase 1, input in Phase 4) to validate feasibility early, instead of building a
   lot only to hit a wall later.
2. **Breadth (platform rollout)** — replicate the reference implementation across **3 agents
   (Win/mac/Ubuntu) + 6 clients (Win/mac/Ubuntu/iOS/Android/Web)**. This is the project's
   **most important goal**; the shared `core/` means each new platform only costs its backend
   work, without touching the protocol.

## Platform rollout

| Platform | Agent | Client | Status | Doc |
|----------|:-----:|:------:|-----------|-----|
| Windows | ✅ | ✅ | **Running for real on two machines over LAN + Tailscale** (Internet/NAT); Phase 0–Phase 6 | 02 / 03 |
| macOS | ✅ | ✅ | **Both roles tested and working** (SCK + VideoToolbox + CGEvent), clean build | 14 |
| Android | — | ✅ | **Video + input** (virtual trackpad, virtual keyboard); in testing on Google Play | 08 |
| iOS | — | ✅ | **Video + input** (SwiftUI + VideoToolbox); in testing via TestFlight | 12 |
| Web | — | 📐 | Design complete, no code yet | 10 |
| Ubuntu | ⬜ | ⬜ | Not started | — |

Matrix + why the agent is desktop-only: `11-platform-transport.md`. The phases below are the
**depth work on Windows**; when opening up a new platform, the `core/` part (Phase 3–Phase 6)
is reused as-is, and only the capture/encode/inject backend (agent) or decode/render/input
backend (client) needs to be written.

## Priority order (depth)

Do the most failure-prone parts first to validate the risks early.

## Phase 0 — Foundation ✅ DONE
- ✅ WGC capture of a game window by process name.
- ✅ Refactor: split into modules — `WindowCapture` (PIMPL, hides winrt), `WindowFinder`, `BmpWriter` (debug).
- ✅ Switched from polling to the `FrameArrived` event (callback on a WGC thread-pool thread).
- ✅ Shared the D3D11 device via `Device()`/`Context()` (pure COM, no winrt leakage) so the encoder can reuse it.
- ✅ `CopyToCpu`/`WriteBmp` split out into `BmpWriter`, only runs when the `--save` flag is set, outside the hot path.
- **Done criteria**: ✅ clean build (0 warnings), event-driven capture, fps measured; hot path never touches the CPU.

**File structure after Phase 0** (now consolidated into `client/windows/capture/`):
```
client/windows/
├── main.cpp                  main: find window → event-driven capture → count frames / measure fps
└── capture/
    ├── CaptureTypes.h        FrameInfo (pure D3D11/COM, no winrt)
    ├── WindowCapture.h/.cpp  capture module, winrt hidden in the .cpp (PIMPL)
    ├── WindowFinder.h/.cpp   find HWND by exe name
    └── BmpWriter.h/.cpp      debug tool: VRAM texture → BMP
```
Run: `client.exe [game.exe] [--save] [--frames N]`

(Historical record — window sharing was removed 2026-07-27: `WindowFinder` is gone and
`WindowCapture` became `ScreenCapture`, display-only.)

## Phase 1 — Encode ✅ DONE (first version, file-based)
- ✅ Defined the `IVideoEncoder` interface + `EncoderConfig` (`IVideoEncoder.h`).
- ✅ GPU selection layer with the chain **NVIDIA → Intel → AMD → CPU (WARP)** (`GpuSelect.cpp`) —
  meeting the requirement of not hard-coding a single GPU type; capture and encoder **share one
  device** on the selected GPU.
- ✅ **Media Foundation** backend (`MfEncoder.cpp`): takes D3D11 textures directly (VRAM) → H.264/MP4.
  MF automatically uses the device's hardware encoder (NVENC on NVIDIA / QSV on Intel) and falls back
  to software when no HW is available → the priority chain is implemented "for free" simply by
  choosing the adapter.
- ✅ Verified for real: captured a 2570×1018 Notepad window on an RTX 5070 Ti → wrote 60 frames to
  `output.mp4` (~1.1 MB, valid `ftyp`/`mp42` boxes).
- **Decision**: chose MF over direct NVENC because `nvEncodeAPI.h` (Video Codec SDK) was not yet
  available on the machine, whereas MF ships with the Windows SDK and already delivered hardware
  encoding on NVENC itself via the MFT. A dedicated NVENC backend (finer low-latency control) can
  be added later **behind the same interface**.
- **Remaining (pushed to Phase 3)**: emit **NAL units for streaming** instead of a file — requires a
  custom `IMFByteStream` or switching to NVENC/async-MFT. `forceKeyframe` is currently a no-op in
  the MF backend.

### NVENC backend ✅ (added, and is the preferred backend)
- ✅ `NvencEncoder.cpp`: loads `nvEncodeAPI64.dll` dynamically (no .lib needed), registers D3D11
  textures zero-copy, preset `P4` + tuning `ULTRA_LOW_LATENCY`, CBR, infinite GOP + on-demand IDR,
  proper `forceKeyframe`=FORCEIDR. Emits **Annex-B NAL** to `.h264` (ready for Phase 3 packetization).
- ✅ `EncoderFactory.cpp`: tries **NVENC → Media Foundation** (matching the NVIDIA→Intel→CPU chain).
- ✅ Verified: RTX 5070 Ti → valid `output.h264` (Annex-B start codes, High-profile SPS,
  7784B IDR + progressively smaller P-frames). Includes a version check: driver older than the
  header → automatic fallback to MF.
- ⚠️ **NVENC version constraint**: the header must be ≤ the API version supported by the driver.
  The current driver supports **API 13.0**; the header is pinned to **13.0**
  (`third_party/nvenc-13.0`, the `sdk/13.0` branch of nv-codec-headers). The official **13.1**
  release at `C:\Tools\Video_Codec_Interface_13.1.15` is only usable **after updating the NVIDIA
  driver** — at that point re-configure with `-DNVENC_INTERFACE_DIR=...` (a cache variable in
  `client/windows/CMakeLists.txt`).

**Files added in Phase 1:** `GpuSelect.h/.cpp`, `IVideoEncoder.h`, `MfEncoder.h/.cpp`,
`NvencEncoder.h/.cpp`, `EncoderFactory.cpp`.
Run: `client.exe game.exe --encode --out out.mp4 --bitrate 20 --fps 60 --frames 300`
(NVENC writes `out.h264`; MF writes `out.mp4`.)

## Phase 2 — In-machine loopback (no network) ✅ DONE
- ✅ `IVideoDecoder` + `DecoderConfig`/`DecodedFrame` (`IVideoDecoder.h`) — symmetric with the
  encoder; in Phase 3 only the NAL source changes from loopback to UDP, the interface stays the same.
- ✅ `MfDecoder.cpp`: **synchronous** H.264 decoder MFT (MFTEnumEx, SYNCMFT) + DXGI device
  manager → **D3D11VA hardware decode**, output **NV12 kept in VRAM** (IMFDXGIBuffer,
  texture pool + array slice, zero-copy). `MF_LOW_LATENCY` enabled so the MFT returns frames
  immediately. Handles `MF_E_TRANSFORM_STREAM_CHANGE` (renegotiate NV12) and `MF_E_NOTACCEPTING`
  (drain).
- ✅ `Renderer.cpp`: preview window + **FLIP_DISCARD** swapchain + `Present(0)`;
  NV12→BGRA conversion + scaling via the **D3D11 Video Processor** (no shaders needed);
  input views cached by (texture, slice); `--save` dumps the backbuffer to `loopback.bmp`.
- ✅ In-process NAL path: added a `PacketHandler onPacket` to `EncoderConfig` —
  NVENC pushes Annex-B to the callback (the `.h264` file becomes optional; empty `outputPath` =
  callback only). The MF encoder rejects `onPacket` (SinkWriter can't emit NAL yet — that's
  Phase 3's job).
- ✅ `--loopback` mode in main: capture → NVENC → MfDecoder → Renderer, all on one
  D3D11 device, QPC timestamps carried end to end to measure latency.
- ✅ **Verified for real** (RTX 5070 Ti, Notepad 2570×1018, 60 frames): the image displays
  correctly again (`window.bmp` vs `loopback.bmp` identical, text sharp), capture→display latency
  **~3.5 ms** steady (average 8.2 ms including the first initialization frame, max 53.9 ms).
- **Threading note**: the entire encode→decode→render chain runs on WGC's FrameArrived thread;
  main only creates the window + pumps messages. The device has `SetMultithreadProtected` enabled.

**Files added in Phase 2:** `IVideoDecoder.h`, `MfDecoder.h/.cpp`, `Renderer.h/.cpp`.
Run: `client.exe game.exe --loopback [--frames N] [--save]`
(without `--frames`: runs until the preview window is closed / ESC is pressed).

## Phase 3 — Transport + Protocol v1 ✅ DONE on one machine (design: `06-transport.md`)
- ✅ **Shared `core/` library** (static lib, namespace `deskhub`) — pure C++20, **no Windows
  headers**, shared across OSes; no threads/sockets/clocks (time is injected from outside via
  `nowUs` → testable offline). Repo structure: `core/` + `client/<os>/`; built with
  **CMake + Ninja**. Complete set: `ByteOrder` + `Wire` (shared 8-byte header with sessionId) +
  `Packetizer` + `Reassembler` + `HostSession`/`ClientSession`.
- ✅ `UdpSocket` (winsock, thin, inside the exe): disables `SIO_UDP_CONNRESET`, SO_RCVBUF 4 MB,
  recvfrom timeout 100 ms. Only this layer is platform-specific.
- ✅ Frame packetize/reassemble: delivers frames **in frameId order**, keeps ≤4 frames being
  assembled, drops a frame after 2 frame intervals or when overtaken by ≥2 newer complete frames;
  after loss, swallows non-IDR frames until an IDR arrives.
- ✅ HELLO/HELLO_ACK/START handshake (500 ms retry); PING every 1 s to measure RTT; BYE +
  5 s timeout on both sides; the host returns to IDLE to wait for a new client after the client
  leaves.
- ✅ **REQUEST_KEYFRAME on packet loss** (250 ms retry until an IDR arrives); `repeatSPSPPS=1`
  (available since Phase 1); `forceIdr` is an atomic flag set from the Recv thread and consumed
  on the next Encode.
- ✅ `--serve` mode (AgentLoop) / `--connect ip[:port]` (ClientLoop, reusing Phase 2's
  MfDecoder/Renderer) / `core_tests` (M1 self-test). The client logs every 1 s:
  fps | kbps | dropped frames | % packet loss | RTT | estimated e2e latency.
- ✅ **Menu-first UX**: run with no arguments → main menu shows this machine's IP per network
  adapter (`NetInfo`, virtual adapters sorted last), `[s]` shares an application (the window
  picker as before), `[c]`/typing `ip[:port]` directly to connect; after the session ends it
  returns to the menu.
- ✅ **Emerged outside the design**: WGC only emits frames when content changes → the agent caches
  the last frame (CopyResource) and re-encodes it from the Recv thread when there is a pending IDR
  request and the source has been static >200 ms — otherwise a client joining a static screen
  would stay black forever.
- ✅ **Verified** (2026-07-20): **M1** `core_tests` PASS (in-order/shuffled/lost/duplicated/
  mid-stream join/timeout + a simulated 2-session handshake, bytes out == bytes in). **M2** 2
  processes over 127.0.0.1 PASS: handshake → image displayed (both static and dynamic sources,
  ~13 fps), 0% packet loss, RTT ~5–10 ms, e2e latency ~4–7 ms; client exits → agent returns to
  IDLE.
- ✅ **M3 two machines over LAN — RAN FOR REAL** (2026-07-22), and beyond: works well **over
  Tailscale** (Internet/NAT), not just LAN. (First run on the host: remember to open UDP 47777 in
  the firewall.)
- ⬜ **Remaining**: **M4** simulate 2–5% drop (clumsy tool), self-recovery via IDR within ≤ a few
  hundred ms.

**Files added in Phase 3:** core: `transport/Packetizer`, `transport/Reassembler`,
`session/HostSession`, `session/ClientSession` (+ `wire/ByteOrder.h`, `wire/Wire` from before),
`tests/CoreTests.cpp`; platform: `deskhubp/Clock.h`; client/windows: `net/UdpSocket`,
`net/NetInfo`, `AgentLoop`, `ClientLoop`.
Run: host machine `client.exe` → `[s]` (or `client.exe game.exe --serve [--port N]`);
viewer machine `client.exe` → type `ip[:port]` (or `client.exe --connect ip[:port]`).

## Phase 4 — Input ✅ code DONE, AWAITING two-machine verification (design: `07-input.md`)
- ✅ **core**: `InputEvent` + build/parse in `Wire`; `InputSender` (batches, assigns seq,
  sends redundantly) / `InputReceiver` (de-duplicates, counts losses) — pure C++20, testable
  offline. `ClientSession::QueueInput` + `HostCallbacks::onInput` wired into the existing state
  machine.
- ✅ **Protocol refinement**: `seq` is attached to **each event** rather than each packet (wire
  layout unchanged). Without it, redundant sends would be interpreted as new actions — pressing W
  once would become three presses. See `04-protocol.md` §4.9 and `07-input.md` §1.
- ✅ **InputCapture** (client): Raw Input on the preview window; the keyboard uses **scancodes**
  (`RAWKEYBOARD.MakeCode`, not WM_KEYDOWN) — DirectInput games read scancodes; sending only vkCode
  means the game doesn't register it. Mouse has 2 modes: absolute (default) and relative
  (F9, locks + hides the cursor) for FPS games. F10 pauses input sending.
- ✅ **InputInjector** (host): `SendInput` with scancodes + `KEYEVENTF_EXTENDEDKEY`; normalized
  coordinates → target window client rect → virtual desktop (`MOUSEEVENTF_VIRTUALDESK`).
  Tracks held keys/buttons → `ReleaseAll()` on BYE/timeout/`SET_FOCUS(false)`/exit.
- ✅ **3-layer stuck-key prevention**: in-packet redundancy + idle-time replay + `ReleaseAll`.
- ✅ **Emerged outside the design — the foreground trap**: `SendInput` injects into whatever window
  is foreground on the host, NOT into a specific HWND. If the machine's owner clicks over to
  another app, the remote controller types straight into their browser/terminal. Tightened: inject
  only when the shared window has focus, otherwise skip + release keys. This both prevents
  mistyped input and matches the semantics of "sharing only this window". *(Removed 2026-07-27
  along with window sharing — with whole displays there is no "outside the scope", so the
  foreground gate is gone; `ReleaseAll` on BYE/timeout/`SET_FOCUS(false)`/exit and the
  "host wins" monitor remain.)*
- ✅ **M1 verification** `core_tests`: wire roundtrip (including negative coordinates), **dropping
  1 in 3 datagrams still applies every event exactly once, in order**, and out-of-order packets
  don't rewind.
- ✅ **M2 verification** 2 processes/1 machine: input travels the full client→host round trip
  (`input 2 (mat 0)`), video shows no regression (e2e ~2.3 ms, 0% packet loss).
- ✅ **M3 — RAN FOR REAL** (2026-07-22): 2 machines over LAN + over Tailscale, successfully
  controlling a **regular application** (typing, mouse movement, full client→host round trip).
- ⬜ **Remaining**: **M4** control a **real game** (mouse look + WASD), measure input latency.
  Input **cannot be tested on one machine**: the agent injects into the foreground window, and if
  the client's preview window is foreground, the injected keys are captured right back by the
  client → a feedback loop.
- ⬜ If a game ignores the input (anti-cheat filtering `LLMHF_INJECTED`) → ViGEm (gamepad, driver
  level) or Interception.
- ⚠️ Game/app running as admin on the host: the agent must be run **as administrator** (UIPI).

**Files added in Phase 4:** core: `InputSender.h/.cpp`, `InputReceiver.h/.cpp` (+ `InputEvent`
in `Wire`); client/windows: `InputCapture.h/.cpp`, `InputInjector.h/.cpp`.
Run: same as Phase 3, input enabled by default. `--noinput` = view-only (can be set on either
role). `client.exe <app> --injecttest` = test the input injection path in isolation, no network
needed (dev).

## Phase 5 — Stability & quality ✅ code DONE, AWAITING two-machine verification
- ✅ **RECONFIG on window resize**. The FrameArrived thread detects the size change → discards the
  encoder + texture cache and rebuilds them right at that frame; the Recv thread sends RECONFIG +
  IDR. The client rebuilds nothing: `MfDecoder` renegotiates by itself via
  `MF_E_TRANSFORM_STREAM_CHANGE`, and `Renderer.EnsureVideoProcessor` follows the decoded frame
  size automatically. RECONFIG only exists to update the display + `HostSession::SetOffer`
  (a client reconnecting afterwards must receive the new numbers).
- ✅ **Encoded dimensions always even**: NV12 chroma is 2×2; an odd-sized window (1689×1392) makes
  `CreateTexture2D(NV12)` return `E_INVALIDARG` → no backend could run on non-NVIDIA machines.
  `EncoderConfig` separates `width/height` (encoded size, even) from `srcWidth/srcHeight` (the
  actual WGC texture, possibly odd) — the video processor needs both to declare the content desc
  correctly; declaring them mismatched makes `CreateVideoProcessorInputView` refuse.
- ✅ **FEEDBACK → bitrate**. `IVideoEncoder::SetBitrate` (NVENC: `nvEncReconfigureEncoder`;
  MF: `CODECAPI_AVEncCommonMeanBitRate`) — no encoder rebuild, so no IDR needed.
  Multiplicative-decrease/additive-increase rule; details in `04-protocol.md` §6.5.
- ✅ **XOR parity FEC** per group of 8 packets (`FEC_PACKET 0x11`): losing 1 packet/group can be
  reconstructed, avoiding dropping the frame + requesting an IDR (an IDR is many times larger than
  a P-frame — answering packet loss with an IDR right when the link is congested is pouring fuel
  on the fire). Dynamically enabled/disabled based on FEEDBACK so the 12.5% overhead isn't paid on
  a clean link.
- ✅ **Client UILayer** (built alongside the Phase 5 GUI work): stats overlay on the preview
  window, 2 buttons for mouse lock / pause sharing the same path as F9/F10.
- ✅ **M1 verification** `core_tests`: 6 FEC cases PASS (recover a middle packet, recover a short
  final packet, recovered frame byte-identical, 2 losses in the same group fall back to the old
  policy, single-packet frame rebuilt from parity, disabled by default). The entire Phase 3/Phase 4
  suite shows no regression even with `kMaxVideoPayload` changing 1176→1174.
- ✅ **Fix series from the 2026-07-21 diagnostic logs** (QSV host → Intel client, clean LAN —
  see `09-diagnostics.md`): (1) **~2fps keepalive when the source is static** — a second
  measurement confirmed it works (host `send 2 fps` steadily while capture is 0); (2) **build the
  decoder on the Decode thread** instead of the Recv thread — a second measurement confirmed the
  client-side `recv_stall` is gone; (3) **PumpAsyncEvents for the async MFT** — QSV pre-queues
  many NeedInput events so output was held hostage behind the event queue, only escaping on the
  next Encode: with sparse input (2fps keepalive) the measured e2e was ~3.4 s = ~7 frames ×
  500 ms; now events are drained right after ProcessInput (+`needInputCredit`), and at sparse
  cadence it waits ≤30 ms for the freshly encoded frame to come out.
- ⚠️ **VBV not taking effect on QSV**: `CODECAPI_AVEncCommonBufferSize` is set yet the IDR is
  still 195 KB (second measurement). Added logging of the IsSupported/SetValue result for each
  CodecAPI property — the next run will show at which step the driver refuses. Additional note: on
  QSV every IDR request = `ReinitTransform` (~200–265 ms, measured `enc_ms_max=265` blocking the
  Recv thread at START) — this cost is one more reason to prefer NACK over requesting an IDR.
- ⬜ **Remaining**: **M3** two machines over LAN — congestion control and FEC have so far only
  been proven on paper + self-tests, never against real packet loss. **M4** simulate 2–5% drop
  (clumsy) to tune the 2%/5% thresholds and the FEC group of 8 against real measurements.
- ⬜ Slicing (multiple slices/frame) **not done**: it only helps if the decoder is willing to
  consume incomplete frames, and `MfDecoder` currently requires complete NAL units. Doing slicing
  without fixing the decode path gains nothing — deferred until M4 measurements show FEC is
  insufficient.

**Files changed in Phase 5:** core: `wire/Wire` (FEC_PACKET, kMaxVideoPayload), `transport/Packetizer`
(parity generation), `transport/Reassembler` (`PushFec`/`TryRecover`), `session/HostSession`
(`SetOffer`, `onFeedback`), `session/ClientSession` (`onReconfig`, `SendFeedback`),
`control/BitrateController` (bitrate tighten/relax policy + FEC on/off hysteresis),
`control/LinkStats` (1 s stats aggregation, builds `Feedback` — shared between Windows/Android);
client/windows: `encode/IVideoEncoder.h` (`SetBitrate`, `srcWidth/srcHeight`),
`encode/MfEncoder`, `encode/NvencEncoder`, `AgentLoop`, `ClientLoop`.
Run self-tests: `make test` (or `out\build\x64-debug\core\core_tests.exe`).

## Phase 6 — Extensions (as needed)
- ✅ **Multiple simultaneous sources** (code done, AWAITING two-machine verification). The host
  shares multiple **displays** on ONE port (since 2026-07-27 displays are the only source kind —
  per-window sharing was removed); the client asks `LIST_SOURCES`,
  ticks its selection, and each source opens its own preview window.
  - **Each (client, source) pair = one independent session** instead of adding a streamId to the
    video header — see `04-protocol.md` §4.1 for the rationale. The video/FEC/input/FEEDBACK
    channels don't change by a single byte; `HostSession`/`ClientSession` remain 1:1.
  - Display capture: `ScreenCapture` (formerly `WindowCapture`) calls `CreateForMonitor` only; an
    `AgentSource` is an HMONITOR plus its name. `InputInjector` maps coordinates against the
    monitor rect. *(The HWND branch of `CaptureTarget` and the foreground guard were removed
    2026-07-27 with window sharing — whole displays have no "outside the scope".)*
  - **The client uses ONE `InputCapture`**, re-attached to whichever preview window is foreground.
    Raw Input registers per *process*, not per window: calling `Attach` a second time with a
    different HWND silently unregisters the first.
  - `Renderer::Pump()` became static: `PeekMessage` doesn't filter by HWND, so one pump loop
    serves every preview window on that thread.
  - ✅ **M1 verification** `core_tests`: SOURCE_LIST round-trip (including UTF-8 names), name
    truncation at correct UTF-8 boundaries, HELLO carrying sourceId, and old-style 13-byte packets
    still parse.
  - ⬜ **Remaining**: M3 two machines — never yet run for real with ≥2 sources.
- 📐 **Web client** (WebTransport + WebCodecs) — **design complete, no code yet**
  (`10-web-client.md`). Runs in the browser, view + input only (like Android v1). Browsers can't
  open raw UDP → the transport is **WebTransport (QUIC datagram)**, which maps 1-1 to UDP so
  `core/` compiled to **WASM** is reused as-is; decoding via **WebCodecs**.
  - **The only core change**: `kMaxVideoPayload` goes from a compile-time constant → a runtime
    parameter based on `maxDatagramSize` (see `04-protocol.md` §6.1).
  - **New native piece**: a WebTransport server on the host side (QUIC/HTTP3, **msquic**
    proposed), feeding datagrams into `HostSession` like UDP.
  - **The hardest part**: self-signed certificates via `serverCertificateHashes` (ECDSA P-256,
    validity < 14 days, SHA-256 hash) + distributing the hash to users — see `10-web-client.md` §6.
  - **Hybrid transport** (native keeps UDP, only web uses QUIC) + the client/host matrix +
    desktop-only host: `11-platform-transport.md`.
  - Milestones: M1 WASM+loopback in a tab → M2 WebTransport echo + certificates → M3 e2e video
    over LAN → M4 input.
- ⬜ Encryption (DTLS/AEAD).
- ⬜ NAT traversal (ICE/STUN/TURN) to run over the Internet.
- ⬜ Audio (Opus + WASAPI loopback).
- ⬜ Adaptive resolution; multi-client.

## Phase 9 — Core foundation for the designed UI 🔶 core DONE, awaiting per-platform wiring

The UI design (desktop + mobile + landing) requires four things `core/` didn't have.
The core part of all four is done and tested; what remains is the socket and UI work of **each**
platform, so this sits in the "awaiting wiring" column, not "not done".

- ❌ **LAN host discovery** — DISCOVER/ANNOUNCE, `HostRegistry`. Built in Phase 9, then
  **REMOVED 2026-07-27** together with the whole auth layer (GĐ10): the app targets
  trusted LANs with typed addresses only. Kept from this work: `Beacon` still answers
  pre-session LIST_SOURCES + PING. History in `git log`.
- ✅ **Out-of-session probe ping** (`PING sessionId=0`) — feeds the alive/latency pair on each
  saved machine card, plus the "link check" panel before pressing Connect. Because
  `Beacon` answers it, it deliberately does **not** feed the session timeout and does **not**
  change the peer address.
- ❌ **Measurable end-to-end latency** — `control/ClockSync`, `LinkStats::AddE2e`,
  `control/LatencyTrace` were built in Phase 9 but **REMOVED 2026-07-27**: no client
  ever wired them in; each ClientLoop ships its own inline estimate instead
  (`e2e = now − (ackDelta − minRTT/2) − frame pts`, seeded from `HelloAck::timebaseUs`).
  See 15-review-todo.md D1. History in `git log`.
- ✅ **Host policy told to the client** — `HELLO_ACK.flags`: whether input is accepted (a
  clipboard flag existed until clipboard sync was removed on 2026-07-27). Previously `allowInput` was just a local variable of each `AgentLoop`; the
  client had no way to know a session was view-only → it still drew the mouse-lock button and the
  virtual keyboard. Now enforced in `HostSession` (one protocol rule, one place to implement) and
  respected by the client in `ClientSession::QueueInput`.
  ⬜ Remaining per platform: the UI hides the input controls when
  `params().inputAccepted` is false.
- ❌ **`SourceInfo.kind`** (window / entire display) on the wire — built in Phase 9 (signaled via
  the `kSourceListFlagKind` header flag), then **REMOVED 2026-07-27**: per-window sharing was
  dropped, every source is a display, so the kind byte and the flag left the wire
  (`04-protocol.md` §4.6). History in `git log`.

**Files added in Phase 9 (core):** `discovery/Beacon.h/.cpp`, `discovery/HostRegistry.h/.cpp`
(HostRegistry removed 2026-07-27), `control/ClockSync.h/.cpp`, `control/LatencyTrace.h/.cpp`
(both removed 2026-07-27); changed: `wire/Wire`
(DISCOVER/ANNOUNCE, `SourceKind` — removed again 2026-07-27, `HelloAck::flags`), `session/HostSession` (the two policy
gates), `session/ClientSession` (`NegotiatedParams::inputAccepted`), `control/LinkStats`.
Tests: `tests/discovery/DiscoveryTests.cpp` + additions in wire/session/control.

### Windows: agent + client per the UI design ✅ RUNNING FOR REAL (one machine)

The first platform to finish wiring Phase 9. The UI was rebuilt from the design project
(`desktop.jsx` / `parts.jsx` / `i18n.jsx` / `theme-light.css`).

- ✅ **native**: `capture/DisplayFinder` (enumerates displays — previously only windows),
  `net/Discovery` (LAN scan, built on `deskhub::HostRegistry`), `net/HostIdent`
  (stable per-machine hostId), `deskhub::Beacon` wired into `AgentLoop`'s Recv loop,
  `SessionSourceRow` with STRUCTURED fields (fps/kbps/rtt/viewer — the `kind` field left with
  window sharing, 2026-07-27) instead of one
  pre-concatenated string. C API bumped to **v2**: `dh_list_displays`, `dh_discover_scan`,
  extended `DhAgentRow`, `DhAgentOptions.shareClipboard` (this whole agent C API was removed
  2026-07-27 with the WinUI3 app; only `dh_client_*` remains).
- ✅ **UI**: `Themes/Tokens.xaml` (light+dark tokens, translated 1-1 from `_ds/tokens/`),
  `Themes/Controls.xaml` (4 button variants, chip, panel, row, pill), `AppState` +
  `Strings` (EN/VI switchable in place), `Controls/` (Sparkline, StatusDot, StatBlock,
  WrapPanel, MachineCard, SourceRow, AddressRow).
- ✅ **screens**: a 74px rail shell (3 items + light/dark button + EN/VI chip) and four screens
  Home / Connect / Share / Viewer. **SharePickerPage + SharingStatusPage merged into a single
  `SharePage`** — the design puts source selection and session status on the same screen, and
  that was the right call: ticking sources on/off mid-session doesn't interrupt viewers.
- ✅ **Verified running for real**: the app starts, light/dark and EN/VI switches apply instantly,
  the share screen lists the real displays + windows correctly (including Vietnamese titles) and
  all three addresses (Tailscale / Ethernet / vEthernet).
- ⬜ **Remaining**: real two-machine run — machine-card alive/latency, the
  "machines viewing" panel, and the end-to-end latency HUD have so far only been proven on one
  machine.
- ⬜ macOS/Android/iOS not yet wired to Phase 9 (core is ready; what remains is socket + UI).

**Note 2026-07-27:** the WinUI3 UI described above was **deleted** (along with the short-lived
ImGui frontend and `deskhub_native.dll`) — the only Windows app is the plain Win32 UI
(`client/windows/win32/`, restored from pre-M4b history, statically linked into one
`Deskhub.exe`); LAN discovery, auth, and per-window sharing were removed project-wide the same
day (see the Phase 9/GĐ10 notes).

⚠️ **A trap that cost time, recorded to avoid tripping twice**: two consecutive hyphens inside a
XAML comment (e.g. when quoting a CSS variable name) are INVALID XML, and the XamlCompiler dies
silently — no line number, no message, just `exited with code 1`.

## Dependency chart

```
Phase 0 capture ─► Phase 1 encode ─► Phase 2 loopback ─► Phase 3 transport ─► Phase 4 input ─► Phase 5 stability ─► Phase 6 extensions
                                                   │
                                        (protocol v1 is defined here,
                                         but the structs should be written from Phase 1)
```

## Guiding principles
1. **Validate risks early**: encode (Phase 1) and input (Phase 4) are the two places most likely to kill the project — touch them early.
2. **Loopback before network**: debugging the codec without network variables in play is far easier.
3. **Interface first, backend later**: `IVideoEncoder`/`IVideoDecoder` allow swapping GPUs without touching the core.
4. **Measure, don't guess**: log fps, latency, bitrate, and packet loss from the start to know you're optimizing the right thing.
