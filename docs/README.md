# Deskhub — Technical Documentation

Technical documentation for **Deskhub** — low-latency remote control & streaming of **any
PC app** (coding, browsing, gaming…), **cross-platform**. The project introduction and
download/run instructions for end users live in the **repo root README**; everything here
is written for engineers working on the codebase, and is derived from the code as it
stands today.

## 🎯 Goals

- **Agent (host): Windows · macOS · Ubuntu** — the machine running the app to be controlled: captures video + receives input.
- **Client: Windows · macOS · Ubuntu · iOS · Android · Web** — the machine that views + controls.

**One app per desktop OS**: each **desktop OS ships ONE app** containing **both roles** (agent +
client), selected at runtime. **iOS / Android / Web are client-only apps** (they cannot
host — input injection and background listening sockets are blocked by those platforms,
see `11-platform-transport.md`). All protocol logic lives in `core/` (pure C++20, no OS
headers); each app adds only a thin OS-specific layer. **Adding a platform = writing a
backend, never touching the core.**

## Platform status

| Platform | Agent (host) | Client | App | Status |
|----------|:------------:|:------:|-----|--------|
| Windows | ✅ | ✅ | `Deskhub.exe` — WinUI 3 shell + `deskhub_native.dll`, both roles | **Real-world use across 2 machines over LAN + Tailscale** (Internet/NAT) |
| macOS | ✅ | ✅ | one app, both roles (SwiftUI + core C++) | **Both roles tested and working** (ScreenCaptureKit + VideoToolbox + CGEvent) |
| Android | ❌ | ✅ | client-only (Kotlin + core C++ via JNI) | **Video + input**; in testing on Google Play |
| iOS | ❌ | ✅ | client-only (SwiftUI + core C++) | **Video + input**; in testing via TestFlight |
| Web | ❌ | 📐 | in the browser (WebTransport + WebCodecs) | Design only — no code yet (`10-web-client.md`) |
| Ubuntu/Linux | ⬜ | ⬜ | one app, both roles (planned) | Not started |

Transport today is **UDP everywhere** (one port, channels multiplexed in the header); the
QUIC/WebTransport path exists only as the web-client design. LAN discovery is a UDP
DISCOVER/ANNOUNCE beacon; Internet/NAT use goes through Tailscale by convention — there is
no built-in NAT traversal. Details: `11-platform-transport.md`.

## Repository layout

```
core/            shared across ALL platforms (pure C++20, NO OS headers)
  include/deskhub/ + src/, by layer:
    wire/        the protocol: byte layout of every message (Wire.h = spec in code,
                 04-protocol.md = spec in prose; the two must change together)
    transport/   Packetizer (fragment + XOR FEC) · Reassembler (reorder/recover/drop)
                 · RetransmitCache (NACK)
    session/     HostSession / ClientSession state machines · ClipboardAssembler
    input/       InputSender / InputReceiver (seq-per-event, loss-tolerant) · KeyMap
    control/     BitrateController · LinkStats · ClockSync · LatencyTrace
    auth/        PasswordAuth (challenge–response) · AuthGuard
    crypto/      Sha256
    discovery/   Beacon (host) · HostRegistry (client) — LAN DISCOVER/ANNOUNCE
  tests/         core_tests — offline, no network/GPU; buildable by every toolchain
platform/        thin OS wrappers core is allowed to use: Clock.h, Random.h
client/windows/  cpp/ (capture WGC · encode NVENC/MF · decode MF+D3D11 · input ·
                 net · C API in DeskhubApi.h) + csharp/ (WinUI 3 UI)      ✅ reference
client/macos/    one app, both roles: cpp/{agent,client,input,net} + swift/   ✅
client/android/  client-only: Kotlin UI + cpp/{decode,net} over JNI            ✅
client/ios/      client-only: SwiftUI + cpp/{decode,net} via ObjC++ bridge     ✅
client/web/      not started — design in 10-web-client.md                      📐
docs/            this documentation
make/            per-platform Makefile includes · scripts/ bootstrap
third_party/     pinned NVENC headers
```

## Requirements & build

- **Windows**: Windows 10 1903+ x64, Visual Studio 2022+ (C++ workload, bundles
  CMake + Ninja), .NET SDK for the WinUI 3 shell. NVIDIA GPU recommended (NVENC);
  otherwise encoding falls back to Media Foundation.
- **macOS / iOS**: macOS 14+, Xcode 26+. The macOS app needs **Screen Recording** (to
  share) and **Accessibility** (to inject input) — `14-macos-app.md`.
- **Any OS**: `make bootstrap` installs every dependency (idempotent), including the
  Android SDK/NDK and pinned format/lint tools.

```
make             # debug build (Windows: deskhub_native.dll + Deskhub.exe)
make run         # build + launch the desktop app
make test        # core_tests — offline, no network/GPU
make lint        # style check for C++/Kotlin/Swift (matches CI)
make build-android / run-android     # Gradle + NDK; run needs a device in `adb devices`
make build-ios / run-ios             # Xcode, Simulator (needs macOS)
make build-macos / run-macos         # Xcode (needs macOS)
make release-windows / release-android / release-ios / release-macos
```

Or drive CMake directly: `cmake --preset x64-debug && cmake --build --preset x64-debug`.
Mobile release automation (fastlane + GitHub Actions): `13-release-mobile.md`.
macOS release (Developer ID + notarization, and why the Mac App Store is closed to the
native app): `16-release-macos.md`.

## Table of contents

| Document | Contents |
|----------|----------|
| [01-architecture.md](01-architecture.md) | System model, video/input data flow, the eight core layers, per-OS backend matrix |
| [02-agent.md](02-agent.md) | Agent (host) role: capture → encode → send, injection hand-off, elevation, beacon, clipboard |
| [03-client.md](03-client.md) | Client role: connect/auth flow, receive → decode → render, per-platform backends |
| [04-protocol.md](04-protocol.md) | **Wire protocol specification** — byte-level layout of every message (source of truth, paired with `Wire.h`) |
| [05-roadmap.md](05-roadmap.md) | Phase-by-phase build log (Windows reference) + platform rollout status |
| [06-transport.md](06-transport.md) | Transport internals: packetization, XOR FEC, NACK/retransmit, pacing, bitrate control, clock sync |
| [07-input.md](07-input.md) | Input pipeline: event model, scancodes, relative mouse, injection, stuck-key safety |
| [08-android-client.md](08-android-client.md) | Android client: Kotlin ↔ JNI ↔ core, MediaCodec, touch/keyboard input |
| [09-diagnostics.md](09-diagnostics.md) | Always-on `[DIAG]` logging: capture per platform, full event catalog, diagnosis cookbook |
| [10-web-client.md](10-web-client.md) | Web client (WebTransport + WebCodecs, core as WASM) — **design proposal, no code yet** |
| [11-platform-transport.md](11-platform-transport.md) | Platform capability matrix, why hosting is desktop-only, UDP strategy, porting recipe |
| [12-ios-client.md](12-ios-client.md) | iOS client: SwiftUI ↔ ObjC++ bridge ↔ core, VideoToolbox, touch/keyboard input |
| [13-release-mobile.md](13-release-mobile.md) | Mobile release & CI: fastlane lanes, GitHub Actions workflows, secrets, versioning |
| [14-macos-app.md](14-macos-app.md) | macOS app (both roles): ScreenCaptureKit, VideoToolbox, CGEvent, permissions |
| [15-review-todo.md](15-review-todo.md) | Open work items from the 2026-07-26 core/platform review: security, correctness, build hygiene |
| [16-release-macos.md](16-release-macos.md) | macOS release: why not the Mac App Store, Developer ID + notarization, dmg, cert setup, CI |

Reading order for newcomers: 01 → 04 → 06 → 07, then the doc for the platform you are
touching. `05-roadmap.md` explains how the codebase got here; `15-review-todo.md` lists
what is known to still need fixing.
