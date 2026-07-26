# 01 — Architecture

Deskhub is a low-latency remote desktop/app tool. One codebase produces one app per OS
(AnyDesk-style): on desktop the same app contains **both roles**; on mobile it is
**client-only**. Everything runs over UDP with a custom protocol (see `04-protocol.md`).
Code comments are written in Vietnamese and carry most of the design rationale — read them.

## 1. System model

Two roles, OS-independent; each OS only swaps the hardware backends underneath:

- **Agent (host role)** — runs where the shared application lives. Captures the screen or a
  window, hardware-encodes H.264, sends video; receives input events and injects them
  locally. Implemented today on **Windows** (`client/windows/cpp/AgentLoop.cpp`) and
  **macOS** (`client/macos/app/cpp/agent/AgentLoop.cpp`).
- **Client role** — receives video, hardware-decodes and renders it, captures mouse /
  keyboard / touch and sends them back. Implemented on **Windows, macOS, Android, iOS**.

Android and iOS have no agent role (mobile OSes cannot host this kind of session), so their
apps ship only the client pipeline. A desktop app can run both roles simultaneously — e.g.
`client/macos/app/cpp/DeskhubBridge.h` keeps two independent singletons, `g_client`
(viewing) and `g_agent` (sharing). The agent can share **multiple sources (windows and/or
displays) on one UDP port**; each (client, source) pair is an independent session with its
own `sessionId` (GĐ6, see `deskhub::SourceInfo` in `core/include/deskhub/wire/Wire.h`).

There is no relay/broker server: clients find hosts by LAN broadcast discovery
(`Discover`/`Announce`) or by a direct `ip:port` address.

## 2. End-to-end data flow

Video path (class names are the actual ones; platform backends vary per §5):

```
AGENT (host)                                              CLIENT
────────────                                              ──────
Capture backend            (WGC / ScreenCaptureKit)
      │  GPU frame
      ▼
HW encoder                 (NVENC / MF / VideoToolbox)
      │  H.264 Annex-B frame (IDR carries SPS/PPS in-band)
      ▼
deskhub::Packetizer        cut into ≤1174-byte fragments,
      │                    optional interleaved XOR FEC
      ├────► deskhub::RetransmitCache   (verbatim copy, answers NACK)
      ▼
UDP socket (+ Pacer on Windows)  ~~~ lossy network ~~~►  UDP socket
                                                              │
                                                              ▼
                                                    deskhub::Reassembler
                                                     reorder / FEC recover /
                                                     NACK plan / drop + wait-IDR
                                                              │  complete frame
                                                              ▼
                                                    HW decoder + renderer
                                                     (MfDecoder+PanelRenderer /
                                                      VtDecoder / MediaCodecDecoder)
```

Input path (opposite direction):

```
CLIENT                                                    AGENT (host)
──────
UI input (mouse / key / touch / soft keyboard via KeyMap)
      │
      ▼
deskhub::InputSender       seq numbering, batch, redundancy
      │                    tail (each event sent ~3×)
      ▼
UDP  ~~~►  deskhub::HostSession ──► deskhub::InputReceiver ──► InputInjector
           (session gate)           (dedupe by seq)            (SendInput / CGEventPost)
```

Control traffic (HELLO/START/PING/FEEDBACK/keyframe requests/clipboard) flows through
`ClientSession` ↔ `HostSession`; feedback drives `BitrateController` on the host.

## 3. The core/ library — one protocol implementation for every OS

`core/` builds as a static library (`core/CMakeLists.txt`) under the one non-negotiable
rule: **pure C++20, no OS headers**. No sockets, no clock reads, no threads, no crypto
libraries — bytes leave via caller-supplied `send` callbacks, time is injected as a `nowUs`
parameter, entropy is passed in by the caller. The reason is stated in `Wire.h`: the same
source is compiled by MSVC, AppleClang, Xcode and the Android NDK, so both ends of the link
run *the same* protocol code — two parallel implementations would drift, and protocol drift
over UDP is nearly undiagnosable. A side effect: everything is testable offline in
`core/tests/` (`core_tests`, no network or GPU needed).

Layers, bottom-up (headers in `core/include/deskhub/<layer>/`):

- **wire/** — `Wire.h`, `ByteOrder.h`. The single definition of what every byte on the wire
  means: protocol v1, 8-byte common header, big-endian fields, `kMaxDatagram = 1200`,
  `MsgType` enum (Hello…Clipboard), and stateless `Build*`/`Parse*` functions. No other
  module may touch raw datagram bytes. `docs/04-protocol.md` is the normative text spec.
- **transport/** — `Packetizer` (host) slices an encoded frame into fragments of exactly
  `kMaxVideoPayload` bytes (offset derived from `pktIndex`) and optionally emits interleaved
  XOR FEC parity (group size `kFecGroupSize = 8`). `Reassembler` (client) reorders and
  reassembles under loss, recovers single-per-group losses from parity, drops frames on a
  deadline, swallows non-IDR frames after loss (infinite GOP ⇒ must re-key), plans NACKs
  (`PlanNack`), and keeps rich loss/burst/late-packet statistics. `RetransmitCache` (host)
  keeps the last 8 frames' datagrams verbatim to answer NACKs — FEC handles isolated loss,
  NACK handles bursts when RTT allows.
- **session/** — `HostSession`: the host-side state machine (IDLE → AUTHENTICATING → READY
  → STREAMING), one client per session in v1, all control-channel handling, callbacks for
  start/keyframe/feedback/disconnect. `ClientSession`: the mirror state machine (Hello →
  Starting → Streaming → Dead) with HELLO/START retransmission, PING/RTT, FEEDBACK and
  keyframe requests. `ClipboardAssembler` reassembles chunked clipboard text (newest update
  wins, size-capped).
- **input/** — `InputSender` (client) assigns sequence numbers and appends a redundancy tail
  of the last 8 events, re-sent twice more (~3× per event in ~50 ms) — the defense against
  the worst failure mode, a lost key-release. `InputReceiver` (host) deduplicates with a
  single `lastAppliedSeq` watermark: no reorder buffer, late input is discarded by design.
  `KeyMap.h` maps soft-keyboard characters to US-layout Windows VK codes for mobile clients.
- **control/** — `BitrateController`: AIMD congestion policy (loss ≥5% ⇒ ×0.75, ≥2% ⇒
  ×0.90, clean link ⇒ +5% probe; also toggles FEC), fed by client FEEDBACK. `ClockSync`:
  min-filter + RTT/2 estimate to convert host frame timestamps into client-clock end-to-end
  latency. `LinkStats`: turns the Reassembler's cumulative counters into per-second windows
  (fps/kbps/loss%) and builds the FEEDBACK payload. `LatencyTrace`: a 60-sample ring
  (320 ms sampling, max-hold) behind the latency sparkline in every client UI.
- **auth/** — `PasswordAuth`: the stateless math of the challenge–response handshake
  (PBKDF2-derived key, HMAC proof over nonce‖clientId; the password never crosses the
  wire). `AuthGuard`: the host-side state — stored `AuthKey`, lockout after repeated wrong
  answers, and trusted-device tokens that skip the password prompt. Lives in core because
  it is a protocol rule; re-implementing it per platform risks one platform silently
  leaving the door open.
- **crypto/** — `Sha256.h`: self-contained SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256 and
  `ConstantTimeEqual`, implemented in-tree because every OS ships a different crypto
  library and core forbids OS headers. Note the stated scope: this authenticates session
  setup only — **the stream itself (video/input/clipboard) is not encrypted**; DTLS/AEAD is
  a roadmap item (`05-roadmap.md`).
- **discovery/** — `Beacon` (host) answers the three pre-session queries (DISCOVER →
  ANNOUNCE, LIST_SOURCES → SOURCE_LIST, PING sid=0 → PONG); it only builds reply bytes, the
  caller sends them back to the datagram's source address. `HostRegistry` (client) merges
  noisy ANNOUNCEs into a stable UI list: keyed by `hostId` (one row per machine even with
  multiple NICs), fixed sort order, 6 s staleness eviction.

## 4. The platform/ thin layer

`platform/include/deskhubp/` is the escape hatch for the two things core needs but must not
implement: it is a header-only CMake INTERFACE library (`platform/CMakeLists.txt`) whose
headers *do* include OS headers, behind `#ifdef`, with one API name everywhere:

- **`Clock.h`** — `NowUs()`: a monotonic microsecond clock (Windows: QPC; elsewhere:
  `clock_gettime(CLOCK_MONOTONIC)`). Every `nowUs` pumped into core, every video timestamp
  and every RTT measurement comes from here. Monotonic because nearly all time math in the
  project is subtraction of two stamps on `uint64_t` — a wall-clock jump would misfire
  timeouts or underflow.
- **`Random.h`** — `RandomBytes()`: kernel CSPRNG (Windows: `BCryptGenRandom`; Apple:
  `arc4random_buf`; Linux/Android: `getrandom(2)` with `/dev/urandom` fallback). Feeds auth
  nonces, salts, session IDs and device tokens. It returns `bool` deliberately — a silently
  all-zero nonce would void the auth layer with no symptom. This is the only piece of the
  auth stack outside core, because entropy can only come from the OS kernel.

Anything larger (sockets, capture, codecs, UI) lives in each client tree, not here.

## 5. Per-OS backend matrix

All verified against the classes in each client directory:

| Stage | Windows | macOS | Android | iOS |
|---|---|---|---|---|
| Capture (agent) | `WindowCapture` — Windows Graphics Capture, window or monitor, D3D11 textures (`client/windows/cpp/capture/`) | `ScreenCapture` — ScreenCaptureKit, NV12 `CVPixelBuffer` (`client/macos/app/cpp/agent/`) | — (client-only) | — (client-only) |
| Encode (agent) | `NvencEncoder` (NVENC, DLL loaded at runtime) with fallback to `MfEncoder` (Media Foundation MFT); chosen by `EncoderFactory` | `VtEncoder` — VideoToolbox, AVCC→Annex-B conversion, SPS/PPS injected per IDR | — | — |
| Decode | `MfDecoder` — sync MFT + D3D11VA, NV12 stays in VRAM | `VtDecoder` — VideoToolbox via `AVSampleBufferDisplayLayer` | `MediaCodecDecoder` — `AMediaCodec` configured directly on the `Surface` | `VtDecoder` — same design as macOS (macOS copy is derived from it) |
| Render | `PanelRenderer` — D3D11 composition swapchain into a WinUI3 `SwapChainPanel` | decode *is* render (layer enqueue) | decode *is* render (`releaseOutputBuffer(..., true)`) | decode *is* render (layer enqueue) |
| Input capture (client) | WinUI3 (C#) UI → `dh_client_mouse_move/…/key` in `DeskhubApi.h` | SwiftUI views → `dh_key/dh_mouse_*` in `DeskhubBridge.h` | touch + soft keyboard (`StreamActivity.kt`, `KeyInputView.kt` → `ClientLoop::QueueCharTap` + core `KeyMap`) | touch + soft keyboard (`TouchInputView.swift`, `KeyInputView.swift`) |
| Input inject (agent) | `InputInjector` — `SendInput` with scancodes (works with Raw-Input/DirectInput games); `LocalInputMonitor` gives the person at the machine priority | `InputInjector` — Quartz `CGEventPost`, `MacKeyMap` translates wire VKs to Carbon keycodes; `LocalInputMonitor` too | — | — |
| Transport glue | `net/UdpSocket` (winsock) + `net/Pacer` (spreads a frame's burst — the project's biggest loss fix), `Discovery`, `Firewall` | `net/UdpSocket`, `SourceQuery`, `NetInfo` | `net/UdpSocket`, `SourceQuery` | `net/UdpSocket`, `SourceQuery` |
| UI / bridge | C# WinUI3 (`client/windows/csharp/`) P/Invokes `deskhub_native.dll` via the flat C API `DeskhubApi.h` | SwiftUI + C bridge `DeskhubBridge.h` (`dh_*` client, `dha_*` agent) | Jetpack Compose/Kotlin → `NativeClient.kt` → `JniBridge.cpp` → `ClientLoop` | SwiftUI → `DeskhubClient.swift` → `DeskhubClient.mm` → `ClientLoop` |

Mobile `ClientLoop`s (Android original, iOS a close port) run three threads — Main (UI /
surface handoff), Net (recv → `ClientSession` + `Reassembler`), Decode — with a bounded
3-frame queue that drops oldest rather than block the receive path.

## 6. Repository layout

```
CMakeLists.txt          root build: core + platform (+ client/windows/cpp on Windows)
CMakePresets.json       CMake presets (x64-debug, ...)
Makefile, make/*.mk     entry points: per-platform build/run/test/format targets
core/                   shared protocol library — pure C++20, no OS headers
  include/deskhub/      wire/ transport/ session/ input/ control/ auth/ crypto/ discovery/
  src/                  mirrors include/ layer by layer
  tests/                core_tests: offline unit tests per layer (CTest)
platform/               header-only OS shims: deskhubp/Clock.h, deskhubp/Random.h
client/windows/
  cpp/                  native pipeline → deskhub_native.dll (agent + client + C API)
  csharp/               WinUI3 frontend (MSBuild/dotnet, not in root CMake)
client/macos/           Xcode app: swift/ (SwiftUI, both roles) + cpp/ (agent/, client/, bridge)
client/android/         Gradle app: src/main/java (Compose UI) + src/main/cpp (NDK client)
client/ios/             Xcode app: swift/ (SwiftUI) + cpp/ (ClientLoop, VtDecoder)
docs/                   this documentation set
scripts/                bootstrap + codestyle scripts (ps1/sh)
third_party/nvenc-13.0  NVIDIA Video Codec SDK headers (NVENC loaded at runtime)
tools/                  ktlint.jar, swiftformat binaries for `make lint/format`
```

There is **no** `host-transport/` or standalone server directory, and no Linux/web client
yet (`10-web-client.md` is a design proposal only; Ubuntu/Linux appears in the roadmap, not
in the tree).

## 7. Where to go deeper

- `02-agent.md` — host role internals (capture/encode loop, multi-source sharing).
- `03-client.md` — client role internals (desktop viewer).
- `04-protocol.md` — the normative wire spec; `Wire.h` must match it.
- `06-transport.md` — packetization, FEC, NACK, loss policy in depth.
- `07-input.md` — input model, key-stuck defense, host-wins arbitration.
- `08-android-client.md`, `12-ios-client.md`, `14-macos-app.md` — per-platform apps.
- `09-diagnostics.md` — stats, overlay, loss forensics (late vs lost packets).
- `11-platform-transport.md` — sockets and platform networking notes.
- `13-release-mobile.md` — mobile release/signing; `05-roadmap.md` — what is not built yet
  (notably stream encryption).
