# 11 — Platforms & Transport Strategy

Cross-cutting decisions that apply to every platform, not to any single app: who can play
which role (Agent/host vs Client), what the transport actually is today, and where the
platform boundary sits in the source tree. Everything below is derived from the current
code. Siblings: 01-architecture.md (layering), 04-protocol.md (wire format),
06-transport.md (transport policy in depth), 10-web-client.md (web design, unimplemented).

## 1. Platform capability matrix

Like AnyDesk, each desktop OS ships **one app containing both roles** (see
`make/windows.mk`, `make/macos.mk`); mobile apps are client-only.

| Platform | Agent (host)? | Client? | App form | Current state |
|---|---|---|---|---|
| Windows | Yes | Yes | WinUI 3 frontend (`client/windows/csharp/`) over `deskhub_native.dll` (`client/windows/cpp/`, C API in `DeskhubApi.h`) | **Both roles shipped.** Full net layer incl. LAN discovery, firewall helper, pacer. |
| macOS | Yes | Yes | SwiftUI app over an Objective-C++ bridge (`client/macos/app/cpp/DeskhubBridge.mm`); host in `cpp/agent/`, client in `cpp/client/` | **Both roles implemented**, not yet verified between two physical machines. LAN discovery not ported yet: no `Discovery` on macOS and `agent/AgentLoop.cpp` does not answer DISCOVER (noted in `client/macos/app/swift/HomeView.swift`). |
| Android | No | Yes | Kotlin UI + NDK `libdeskhub.so` (`client/android/app/src/main/cpp/ClientLoop.cpp`) | **Client-only, shipped to store testing** (see 13-release-mobile.md, `make/android.mk`). |
| iOS | No | Yes | SwiftUI + C++ (`client/ios/app/cpp/ClientLoop.cpp`, VideoToolbox decode) | **Client-only, shipped to store testing** (Simulator build via `make/ios.mk`; device/App Store via Xcode). |
| Web | No | Planned | Browser (WebTransport + WebCodecs + WASM `core/`) | **Design only** — see 10-web-client.md. No web code exists in the repo. |
| Linux | Planned | Planned | — | **No code.** Named only as a future target (root `CMakeLists.txt` comment; `core/` and the Android `UdpSocket.cpp` are already Linux-compatible). |

## 2. Why only desktops can host

The host role needs three OS capabilities that only desktop OSes grant to a third-party
app:

1. **Synthetic input injection.** The agent must inject remote mouse/keyboard events
   system-wide: `SendInput` on Windows (`client/windows/cpp/input/`), CGEvent posting on
   macOS (`client/macos/app/cpp/agent/InputInjector.mm`, gated by the Accessibility
   permission — `agent/Permissions.mm`). iOS has no API for this at all; Android would
   require an AccessibilityService with severe restrictions. No injection, no remote
   control.
2. **Unattended, long-lived capture and listening.** A host binds a UDP port and answers
   whenever a client shows up. Desktop processes may listen indefinitely; mobile OSes
   suspend backgrounded apps and their sockets. Screen capture is similarly gated: macOS
   needs the Screen Recording permission (`agent/ScreenCapture.mm`), which a desktop app
   can hold persistently; iOS/Android offer no equivalent entitlement for a background
   remote-control host.
3. **Binding a fixed, advertised port.** The host must listen on a well-known port
   (default 47777) that the user can read out to the other machine. The macOS
   `UdpSocket.cpp` header comment records this explicitly: the host role calls
   `Open(port)` with a fixed port — something the iOS sandbox does not permit — while
   clients everywhere pass `Open(0)` and take an ephemeral port.

## 3. Transport strategy as implemented

- **UDP everywhere, native sockets.** Every platform speaks raw UDP through its own thin
  `UdpSocket` wrapper — winsock2 on Windows, BSD sockets on Android/iOS/macOS (§4).
  There is no TCP data path and no reliability layer below `core/` (retransmit/FEC/NACK
  live in `core/` — see 06-transport.md).
- **One port, channel multiplexing.** All traffic — control, video, input — shares a
  single socket and port, demultiplexed by the `chan` byte of the common header
  (04-protocol.md §2). Default host port **47777**; if busy, the host walks forward up
  to 64 ports (`FindFreeUdpPort` in `client/windows/cpp/net/UdpSocket.cpp`; an inline
  `kPortTries = 64` loop in `client/macos/app/cpp/agent/AgentLoop.cpp`) and displays the
  port it actually bound.
- **LAN discovery = UDP broadcast, split core/platform.** Protocol logic is shared in
  `core/`: `deskhub::Beacon` (`core/include/deskhub/discovery/Beacon.h`) builds host-side
  replies to DISCOVER / LIST_SOURCES / pre-session PING; `deskhub::HostRegistry`
  (`core/include/deskhub/discovery/HostRegistry.h`) merges ANNOUNCEs per `hostId`,
  orders them stably, and expires stale hosts. The socket side is per-platform: on
  Windows, `ScanForHosts` (`client/windows/cpp/net/Discovery.cpp`) sends DISCOVER to the
  **directed broadcast address of every adapter** (`ListLocalBroadcasts` in
  `net/NetInfo.h`) and collects replies for ~1.2 s. Today this is **end-to-end on
  Windows only**; macOS, Android and iOS connect by typed address and still query
  sources pre-session via their `net/SourceQuery.cpp`.
- **NAT / Internet: Tailscale, not built-in traversal.** Verified: the repo contains
  **no STUN, TURN, ICE, hole-punching or relay code**. The strategy, recorded in code
  comments (`core/include/deskhub/wire/Wire.h`, `client/macos/app/cpp/net/NetInfo.h`),
  is to let a VPN such as Tailscale provide a flat address space; the user types the
  Tailscale address manually (broadcast discovery cannot cross it — a /32 has no
  broadcast address, per `client/windows/cpp/net/Discovery.h`). `NetInfo` deliberately
  lists `utun*`/VPN interfaces so that path stays visible.
- **QUIC / WebTransport: design only.** Verified by grepping `msquic`, `quic`,
  `WebTransport` across the repo: **zero hits in source code** — the terms appear only
  in docs. The plan to carry the same datagram protocol over WebTransport (QUIC
  unreliable datagrams) for the browser client is specified in 10-web-client.md and is
  entirely unimplemented; no msquic or any other QUIC library is vendored or linked.

## 4. `UdpSocket` — four deliberate copies of one API

The socket wrapper is intentionally **duplicated per platform with an identical API**,
so `AgentLoop`/`ClientLoop` code reads the same everywhere and porting is copying, not
rewriting (stated in each header):

| Path | Backend | Lineage |
|---|---|---|
| `client/windows/cpp/net/UdpSocket.{h,cpp}` | winsock2 | original Windows version |
| `client/android/app/src/main/cpp/net/UdpSocket.{h,cpp}` | BSD sockets ("Android/Linux") | POSIX original |
| `client/ios/app/cpp/net/UdpSocket.{h,cpp}` | BSD sockets | copied from Android, no code changes |
| `client/macos/app/cpp/net/UdpSocket.{h,cpp}` | BSD sockets | copied from iOS, no code changes |

Shared conventions (all four):

- `NetAddr` holds IPv4 in **host byte order**; `htonl`/`ntohl` happen only at the
  `sockaddr_in` boundary inside the `.cpp`. `Pack()`/`Unpack()` squeeze an address into
  a `u64` so two threads can share it via `std::atomic` (peer roaming in `AgentLoop`).
- `RecvFrom` is tri-state: `>0` bytes, `0` timeout/benign error, `<0` fatal — the
  contract that lets one blocking net thread interleave receives with periodic `Tick`.
- `Open()` sets a **4 MB `SO_RCVBUF`** (burst absorption at high bitrate) and binds
  `INADDR_ANY` (multi-homed machines; you cannot predict the peer's interface).
- `SetRecvTimeout` maps to `SO_RCVTIMEO`; benign errors are folded to `0`
  (POSIX: `EAGAIN`/`EWOULDBLOCK`/`EINTR`/`ECONNREFUSED`).

Windows-only differences: `WSAStartup`/`WSACleanup` lifecycle owned by the object;
`SIO_UDP_CONNRESET` disabled in `Open()` (otherwise one ICMP port-unreachable makes
`recvfrom` fail with `WSAECONNRESET` forever), with `RecvFrom` swallowing
`WSAETIMEDOUT`/`WSAECONNRESET`/`WSAEMSGSIZE` as a second line of defense; timeout as a
`DWORD` rather than a `timeval`; plus API extras the other platforms don't have yet:
`SetBroadcast` (required for discovery — winsock rejects broadcast `sendto` with
`WSAEACCES` otherwise), `FindFreeUdpPort`, and `lastBindAddrInUse()` (distinguishes
"port taken by an old host" for a friendly UI message). The POSIX copies differ from
each other only in comments; iOS/macOS additionally require the Local Network permission
(`NSLocalNetworkUsageDescription` in the Xcode projects) at the app-bundle level.

## 5. Per-platform network-layer notes

**Blocking model (all platforms):** one dedicated net thread per session runs a
blocking `RecvFrom` loop with `SO_RCVTIMEO` as the tick clock — 100 ms on the Windows
host (`AgentLoop.cpp`), 10 ms on the Android/iOS/macOS client loops (`ClientLoop.cpp`
in each). No epoll/kqueue/IOCP; a single socket per session doesn't need them.

**Windows extras** (`client/windows/cpp/net/`):

- `Firewall.{h,cpp}` — `HostFirewallRulePresent` / `EnsureHostFirewallRule`: creates an
  inbound-UDP allow rule for the exe via COM (`INetFwPolicy2`), covering all three
  profiles; adding requires admin, so it rides the Share button's UAC elevation
  (`ElevatedShare.h`). Cures the classic "host reachable but every HELLO times out".
- `NetInfo.{h,cpp}` — `ListLocalIPv4` (per-adapter addresses for the "your address" UI,
  filtering loopback/APIPA) and `ListLocalBroadcasts` (per-adapter directed broadcast
  for discovery). macOS has its own `NetInfo` (getifaddrs-based, maps `en0`/`utun*`
  device names to friendly labels); Android/iOS have none.
- `HostIdent.{h,cpp}` — `LocalHostId()` (stable 32-bit machine id hashed from the
  registry `MachineGuid`, fallback: hashed hostname) and `LocalHostName()`. Feeds
  `Beacon`'s ANNOUNCE and lets the scanning client exclude itself. Not a security
  identifier.
- `Pacer.{h,cpp}` — rate-limits `sendto` on the host's dedicated send thread so an IDR
  burst doesn't tail-drop at a Wi-Fi bottleneck (rationale and measurements in the
  header; policy discussion in 06-transport.md). Windows-only today; the macOS
  `AgentLoop` has no equivalent class yet.
- `Discovery.{h,cpp}` / `SourceQuery.{h,cpp}` — blocking one-shot scans (~1.2 s / ~3 s),
  called off the UI thread (`Task.Run` from C#), exposed as `dh_discover_scan` /
  `dh_client_list_sources` in `DeskhubApi.h`.

## 6. The `core/` boundary — what a platform must provide

`core/` (static lib, namespace `deskhub`, pure C++20) **never touches an OS header** —
that is the property that lets one source tree compile for Windows, Android NDK, iOS
and macOS (`core/CMakeLists.txt`). Its entire contact surface with a platform:

1. **Bytes out** — a `send(span<const uint8_t>)` callback: core builds a datagram and
   hands it over; the caller does the `sendto`.
2. **Bytes in** — the caller feeds each received datagram into `HandlePacket(...)`.
3. **Time** — every stateful class takes `nowUs` as a parameter; core owns no clock
   and no threads (which is why `core_tests` runs offline).

Two OS-touching services live in `platform/` (`platform/include/deskhubp/`, an
INTERFACE library of header-only inlines — `platform/CMakeLists.txt`):

- `Clock.h` — `NowUs()`, monotonic microseconds: QueryPerformanceCounter on Windows,
  `CLOCK_MONOTONIC` elsewhere. Consumed by session logic (injected), frame timestamps,
  RTT measurement.
- `Random.h` — `RandomBytes()`, kernel CSPRNG for nonces/salts/sessionIds:
  `BCryptGenRandom` (Windows), `arc4random_buf` (Apple), `getrandom(2)` with a
  `/dev/urandom` fallback (Linux/Android).

The **socket layer deliberately lives outside both** — in each client app's `net/`
directory (§4). `platform/CMakeLists.txt` notes the intent to eventually fold the
per-OS `UdpSocket.cpp` into `platform/` as a STATIC library; today it is per-app.

Build wiring: the root `CMakeLists.txt` adds `core/`, `platform/`, and (on Windows)
`client/windows/cpp/`; Android's Gradle/NDK build adds `core/` itself
(`client/android/app/src/main/cpp/CMakeLists.txt`); macOS/iOS build through their Xcode
projects via `make/macos.mk` / `make/ios.mk`; shared core targets (tests, coverage) are
in `make/core.mk`.

## 7. Adding a new platform (e.g. Linux), concretely

Reused as-is: **`core/`** (wire, transport, session, input, control, discovery, crypto,
auth) and **`platform/`** (`Clock.h` and `Random.h` already have Linux branches).

To reimplement, following the existing pattern:

1. **`net/UdpSocket.{h,cpp}`** — copy the Android version verbatim; its own header
   labels it "BSD socket (Android/Linux)". Add `SetBroadcast`/`FindFreeUdpPort` from
   the Windows header if discovery and the host role are wanted.
2. **`net/SourceQuery.cpp`** — copy from any client (all four are parallel copies).
3. **Client role** — a `ClientLoop` following `client/macos/app/cpp/client/ClientLoop.cpp`
   (net thread, 10 ms recv timeout, feed `ClientSession`) plus a hardware decoder
   (VA-API/Vulkan video filling the role VideoToolbox/MediaCodec/D3D11 play today).
4. **Host role** — an `AgentLoop` (port walk-forward, `Beacon` for discovery replies),
   screen capture + encoder (PipeWire/VA-API), input injection (uinput/XTEST), and the
   Linux analogues of the Windows conveniences: a `NetInfo` (getifaddrs — the macOS one
   is nearly reusable), a `HostIdent` (e.g. `/etc/machine-id`), and send pacing (port
   `Pacer`, whose logic is not Windows-specific despite its location).
5. **UI + glue** — a frontend over either the C API pattern (`DeskhubApi.h`) or the
   bridge pattern (`DeskhubBridge.mm`), plus a `make/linux.mk` and a `client/linux`
   entry in the root `CMakeLists.txt`.

No transport work is required: the wire protocol (04-protocol.md) and everything above
the socket already compile for Linux unchanged.
