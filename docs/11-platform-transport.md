# 11 — Platforms & Transport Strategy

Cross-cutting decisions that apply to every platform, not to any single app: who can play
which role (Agent/host vs Client), what the transport actually is today, and where the
platform boundary sits in the source tree. Everything below is derived from the current
code. Siblings: 01-architecture.md (layering), 04-protocol.md (wire format),
06-transport.md (transport policy in depth), 10-web-client.md (web design, unimplemented).

## 1. Platform capability matrix

Each desktop OS ships **one app containing both roles** (see
`make/windows.mk`, `make/macos.mk`); mobile apps are client-only.

| Platform | Agent (host)? | Client? | App form | Current state |
|---|---|---|---|---|
| Windows | Yes | Yes | Plain Win32 app (`client/windows/win32/`) — ONE statically linked exe, standard Windows controls; host role drives `AgentLoop` directly, client role goes through the C API (`client/windows/cpp/DeskhubApi.h`, `dh_client_start_hwnd`). (The WinUI 3 frontend and `deskhub_native.dll` were removed 2026-07-27.) | **Both roles shipped.** Full net layer incl. firewall helper, pacer. |
| macOS | Yes | Yes | SwiftUI app over an Objective-C++ bridge (`client/macos/app/cpp/DeskhubBridge.mm`); host in `cpp/agent/`, client in `cpp/client/` | **Both roles tested and working.** |
| Android | No | Yes | Kotlin UI + NDK `libdeskhub.so` (`client/android/app/src/main/cpp/ClientLoop.cpp`) | **Client-only, shipped to store testing** (see 13-release-mobile.md, `make/android.mk`). |
| iOS | No | Yes | SwiftUI + C++ (`client/ios/app/cpp/ClientLoop.cpp`, VideoToolbox decode) | **Client-only, shipped to store testing** (Simulator build via `make/ios.mk`; device/App Store via Xcode). |
| Ubuntu | Yes | Yes | GTK3 UI over a GTK-free C++ layer (`client/linux/gtk/` + `client/linux/cpp/`) — ONE `deskhub` executable, both roles | **Both roles working**, verified between two machines over LAN. Capture needs xdg-desktop-portal; injection needs `/dev/uinput`. See 17-linux-app.md. |
| Web | No | Planned | Browser (WebTransport + WebCodecs + WASM `core/`) | **Design only** — see 10-web-client.md. No web code exists in the repo. |

## 2. Why only desktops can host

The host role needs three OS capabilities that only desktop OSes grant to a third-party
app:

1. **Synthetic input injection.** The agent must inject remote mouse/keyboard events
   system-wide: `SendInput` on Windows (`client/windows/cpp/input/`), CGEvent posting on
   macOS (`client/macos/app/cpp/input/InputInjector.mm`, gated by the Accessibility
   permission — `agent/Permissions.mm`), `/dev/uinput` virtual devices on Ubuntu
   (`client/linux/cpp/input/InputInjector.cpp`, gated by a udev rule). iOS has no API for
   this at all; Android would require an AccessibilityService with severe restrictions.
   No injection, no remote control.
2. **Unattended, long-lived capture and listening.** A host binds a UDP port and answers
   whenever a client shows up. Desktop processes may listen indefinitely; mobile OSes
   suspend backgrounded apps and their sockets. Screen capture is similarly gated: macOS
   needs the Screen Recording permission (`agent/ScreenCapture.mm`), which a desktop app
   can hold persistently; Ubuntu/Wayland needs an `xdg-desktop-portal` session, which is
   the weakest of the three — it is granted per sharing session rather than persistently,
   so the system dialog reappears every time (`17-linux-app.md` §2); iOS/Android offer no
   equivalent entitlement for a background remote-control host.
3. **Binding a fixed, advertised port.** The host must listen on a well-known port
   (always 47777) that the user can read out to the other machine. The macOS
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
  (04-protocol.md §2). Host port is **fixed at 47777** (`kDeskhubPort`, defined once per
  platform in `net/UdpSocket.h`). If it is busy the host **fails with an explicit error**
  instead of binding elsewhere — the port-walking of earlier versions (`FindFreeUdpPort`,
  a `kPortTries = 64` loop) was deleted 2026-07-27, because clients only ever type a bare
  IP: a host that quietly moved to 47778 was a host nobody could reach.
- **No LAN discovery, no auth — removed 2026-07-27.** The DISCOVER/ANNOUNCE broadcast
  beacon, `HostRegistry`, and the whole password/auth layer (GĐ10) were removed: the
  app targets trusted LANs, and every connection starts from a typed `ip:port` read
  off the host's share screen. What remains of `core/discovery` is `deskhub::Beacon`,
  now only answering pre-session **LIST_SOURCES** (feeds the source picker) and
  **PING** probes on the host's session socket.
- **NAT / Internet: Tailscale, not built-in traversal.** Verified: the repo contains
  **no STUN, TURN, ICE, hole-punching or relay code**. The strategy, recorded in code
  comments (`core/include/deskhub/protocol/Wire.h`, `client/macos/app/cpp/net/NetInfo.h`),
  is to let a VPN such as Tailscale provide a flat address space; the user types the
  Tailscale address manually. `NetInfo` deliberately lists `utun*`/VPN interfaces so
  that path stays visible.
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
`FindFreeUdpPort`, and `lastBindAddrInUse()` (distinguishes
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
  filtering loopback/APIPA). macOS has its own `NetInfo` (getifaddrs-based, maps
  `en0`/`utun*` device names to friendly labels); Android/iOS have none.
- `Pacer.{h,cpp}` — rate-limits `sendto` on the host's dedicated send thread so an IDR
  burst doesn't tail-drop at a Wi-Fi bottleneck (rationale and measurements in the
  header; policy discussion in 06-transport.md). Windows-only today; the macOS
  `AgentLoop` has no equivalent class yet.
- `SourceQuery.{h,cpp}` — blocking one-shot query of what the host shares (~3 s),
  called off the UI thread, exposed as `dh_client_list_sources` in `DeskhubApi.h`.

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
`client/windows/cpp/` + `client/windows/win32/`; Android's Gradle/NDK build adds `core/` itself
(`client/android/app/src/main/cpp/CMakeLists.txt`); macOS/iOS build through their Xcode
projects via `make/macos.mk` / `make/ios.mk`; shared core targets (tests, coverage) are
in `make/core.mk`.

## 7. Adding a new platform, concretely

Reused as-is: **`core/`** (wire, transport, session, input, control) and **`platform/`**
(`Clock.h`/`Random.h` already branch per OS).

The recipe below is written from the **Ubuntu port**, which is the most recent one and
the only one where the OS refused two of the capabilities outright — so it exercises
every step (`client/linux/`, `17-linux-app.md`):

1. **`net/UdpSocket.{h,cpp}`** — copy from any POSIX client verbatim; its own header
   labels it "BSD socket". Ubuntu changed one comment and nothing else.
2. **`net/SourceQuery.cpp`, `net/NetInfo.cpp`** — copy from macOS; only the
   friendly-name table for network interfaces needs rewriting.
3. **Client role** — a `ClientLoop` following `client/macos/app/cpp/ClientLoop.cpp` (net
   thread with a 10 ms recv timeout feeding `ClientSession`, decode thread behind a
   3-frame queue) plus a hardware decoder and, if the platform has no "decode is render"
   layer, a renderer of your own.
4. **Host role** — an `AgentLoop` (`Beacon` for pre-session LIST_SOURCES/PING replies,
   one Recv thread routing packets to per-source pipelines), screen capture, an encoder,
   and input injection.
5. **UI + glue** — a frontend over the C API pattern (`DeskhubApi.h`), the bridge pattern
   (`DeskhubBridge.mm`), or direct calls (Ubuntu: GTK3 calls `AgentLoop`/`ClientLoop`
   straight, and the C++ layer never includes GTK). Plus a `make/<os>.mk` and an entry in
   the root `CMakeLists.txt`.

**No transport work is required** — the wire protocol (04-protocol.md) and everything
above the socket compiled for Linux unchanged, which is the breadth axis working as
designed.

Three lessons from the Ubuntu port worth carrying to the next platform:

- **Capture may not be enumerable.** On Wayland the OS runs the source picker, so
  `GetShareSources()` had to become "show the dialog and report the answer" rather than
  "list what exists". Any platform with a permission broker will do the same.
- **The static-source problem is universal.** Every compositor so far only emits a frame
  when content changes, so a client joining a still screen sees black forever unless the
  agent can re-encode the last frame. Windows and macOS keep a copy; Linux re-encodes the
  encoder's existing NV12 surface, which is cheaper — check for that option first.
- **Encoder and decoder need not use the same library.** Writing the encoder directly on
  the platform API is reasonable (you choose the stream parameters); writing the decoder
  directly often is not (you must parse someone else's stream and manage a DPB).
