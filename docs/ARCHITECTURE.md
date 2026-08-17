**English** · [Tiếng Việt](ARCHITECTURE.vi.md)

# Deskhub — Architecture

This document describes **how** Deskhub is built: the layers, the processes and
threads, the wire protocol, and the design decisions behind them. What the product
does, as a user sees it, lives in [`SPECIFICATION.md`](SPECIFICATION.md); the threat
model lives in [`SECURITY.md`](../SECURITY.md).

- **Status:** describes the current code.
- **Audience:** anyone changing the code.

---

## 1. Layers

One rule drives the whole layout: logic is written once and shared by every client.

```
core/       pure C++20, no OS headers, no third-party code, unit-tested offline
platform/   thin OS abstractions behind one identical API per header (depends on core)
client/     per-OS apps: windows, linux, macos, ios, android (depend on platform + core)
```

| Layer | Contents |
| --- | --- |
| `core/protocol` | Wire format (`Wire.h`), record framing for streams (`RecordStream.h`), packet classifier that tells QUIC from Deskhub beacon datagrams |
| `core/transport` | Packetizer/Reassembler for video, FEC, retransmit cache, send pacer |
| `core/session` | Host/client session state machines, viewer table, clipboard sync, terminal session table, auth attempt throttle |
| `core/control` | Bitrate controller, quality ladder, stream sizing, clock offset |
| `core/terminal` | The VT emulator every client shares: `VtParser`, `Screen`, `KeyEncoder`, `Palette` |
| `core/net` | Trust store (client side), paired devices (host side), bind-address selection, LAN scan logic |
| `core/ui` | Every user-visible string, settings parsing, table-row builders — so all five clients say the same things |
| `platform/net` | `UdpSocket` (per-OS), `QuicEndpoint` (quiche behind a pimpl), `SessionTransport`, source query, host probe, LAN scanner |
| `platform/session` | `HostEngine`, `HostNetLoop`, `ClientEngine`, `TerminalHost`, `TerminalViewer`, `AuthNegotiation` |
| `platform/system` | Clock, random, PTY (ConPTY / forkpty), host identity (keys), trust/paired-device files, autostart, keep-awake |
| `client/<os>` | Capture, encode, decode, render, windowing, dialogs — nothing protocol-shaped |

`core/` must stay testable offline with no network and no GPU. `platform/` may touch
the OS but must expose one identical API everywhere. If the same code appears in two
clients, it belongs in a lower layer.

## 2. One port, one transport

Everything a host offers rides **one UDP port** (default 47777) through one
`SessionTransport`, which wraps a single `QuicEndpoint`:

```
                      UDP port 47777
                            |
                 ClassifyPacket (first byte)
                   /                    \
            QUIC packets          Deskhub datagrams
                 |                        |
   +-------------+------------+       beacon only:
   |             |            |       LIST_SOURCES / PING answered
 streams     datagrams     (TLS)      in the plain; every other
   |             |                    raw packet is dropped
 control      video
 input                    Streams carry framed records (RecordStream):
 clipboard                length-prefixed messages up to 16 KiB.
 terminal                 Datagrams carry one video packet each (≤ 1200 B).
```

- **Streams** (reliable, ordered): control, input, clipboard, terminal — each
  connection uses one bidirectional stream, opened by the client. A stuck stream on
  one connection cannot stall another connection.
- **Datagrams** (unreliable, unordered, still encrypted): video packets. Lost ones
  are never retransmitted by QUIC; the app's own FEC/NACK machinery handles loss.
- **Raw UDP** exists only for discovery: the beacon answers scanners that speak no
  QUIC, and probes it did not invite get an empty source list. Inbound raw packets
  that are not discovery types are discarded before they reach any session code.

`QuicEndpoint` hides quiche completely (pimpl; `QuicEndpointNone.cpp` stubs it out,
but only when a build opts out with `-DDESKHUB_QUIC=OFF` — a missing quiche fails the
configure, because a stub binary cannot share or connect). Connections are identified by peer address; there is no
connection migration. A quiche connection is single-threaded by contract, so every
touch of the endpoint happens under the transport's send mutex — and the transport
never holds that mutex across a blocking socket wait (`WaitReadable` first, unlocked;
then a brief locked `Poll`). Holding it across the wait starves every sender.

## 3. Admission: pairing

Every machine creates an ECDSA P-256 key on first run (`HostIdentity`); its SHA-256
SPKI hash is the fingerprint people see. TLS uses a self-signed certificate over that
key. On top of TLS, an application-level handshake (`AuthNegotiation`) decides
admission per connection. The transport runs it and drops every message from a
connection whose auth has not settled:

| Client offers | Host knows the machine | Result |
| --- | --- | --- |
| nothing | paired | **Signature**: client signs a nonce+host-fingerprint transcript with its key. In silently. |
| nothing | unknown | **Approval**: the person at the host is asked (*Let this machine in?*). |
| a passcode | host has one | **Passcode**: SPAKE2 over a salted verifier — the code never travels, one guess per connection, both sides prove it, MACs are bound to the host key the client actually saw (kills relays). A typed code is always checked, paired or not. |
| a passcode | host has none | nothing to check against → Signature if paired, Approval otherwise. |
| anything | pairing switched off | **Denied** (paired machines still get Signature). |

Success writes the client into the host's `paired_devices`; pairing is by key, not
address. Three wrong passcode guesses lock the passcode path for 30 seconds
(`AuthThrottle`, shared constants with the legacy session lockout); the approval path
needs no throttle — a human is the gate.

Client side, `known_hosts` (`TrustStore`) pins host keys. A **changed** key blocks the
connection behind a loud warning; an unknown key is settled by the handshake itself
(a host that proved the passcode is remembered without a prompt).

The wire carries the public key itself, never a bare fingerprint — the host hashes
what it receives, so wearing someone else's identity would mean signing with a key
the impostor does not hold. And because admission settles once per connection,
nothing above the transport ever asks again: a machine that proved itself carries no
passcode in any later message, and session code treats the whole connection as
authenticated.

## 4. Host side

```
HostEngine (one per app, owns SessionTransport)
 ├─ net-loop thread: RunHostNetLoop
 │    recv → beacon replies | video-path ingest | Chan::Terminal → TerminalHost
 │    per-source session Tick, clipboard flush, reconfig, stats
 ├─ capture/encode: per-source, driven by the OS capture callbacks (client layer)
 │    frame → encoder (per-source mutex) → Packetizer → FEC → SendTo (datagrams)
 └─ TerminalHost (tenant, when the terminal is shared)
      ├─ HandleMessage on the net-loop thread: TERM_OPEN/DATA/RESIZE/CLOSE → PTY
      └─ pump thread: PTY output → host-side Screen mirror + TERM_DATA records,
           expiry, kicks
```

- The engine runs whenever anything is shared. With zero screen sources and the
  terminal ticked it runs source-less; the loop stays alive while the terminal does.
- Each screen source is a `SourcePipelineState`: its own `HostSession` (viewer table,
  negotiation, input arbitration), encoder, quality ladder and diagnostics. One
  encode feeds every viewer of that source.
- The feedback loop: viewers send `Feedback` (loss/RTT) once a second; the host's
  `BitrateController` (AIMD) and `QualityLadder` adjust encoder bitrate, resolution
  and fps; FEC toggles on loss. quiche's CUBIC congestion control sits underneath the
  datagram path; the two act in series — quiche bounds what leaves the machine, the
  app adapts the encoder to the loss that results.
- Input: "host wins" — `LocalInputMonitor` pauses remote input while the person at
  the machine moves their own mouse; one viewer drives at a time.
- Shells: one PTY per shell (`ConPTY` on Windows, `forkpty` elsewhere), at most 8; a
  dropped connection detaches the shell and keeps the PTY alive for 2 minutes so the
  same machine can reattach. Every open/close/detach/reattach is audit-logged with
  address, name and key.
- Every shell's output also feeds a host-side `core/terminal` Screen from the moment
  it starts. *Stop & attach* disconnects the remote client and opens that mirror —
  scrollback intact — in a terminal window on the host; a shell taken over this way
  belongs to the host, never expires, and ends when the host's window closes.

## 5. Client side

```
ClientEngine (one per viewer window)          TerminalViewer (one per shell window)
 ├─ net thread: trust check → auth →          ├─ own thread: connect → trust check →
 │   HELLO/negotiation → video ingest         │   auth → TERM_OPEN → record pump
 │   (Reassembler+FEC), NACKs, feedback       ├─ core/terminal Screen holds the grid
 └─ decode thread: decoder + render queue     └─ UI polls Snapshot(), posts keys
```

Both follow the same rule as the host: the QUIC connection lives on one thread; the
UI posts intents (keys, resize, accept-fingerprint) into a command queue. The
terminal window never parses escape sequences — `core/terminal` turns the byte
stream into a cell grid, and the window only draws cells and forwards key events.

## 6. Discovery

The beacon answers `LIST_SOURCES` and `PING` as plain UDP so a scanner can sweep a
subnet without 254 TLS handshakes. A stranger's reply is an empty list; the real
source list is revealed only over an admitted connection. That answer also carries
what the host can do — whether it takes input, whether it shares a terminal — in the
`SOURCE_LIST` header flags, so a client knows before it opens any window that a phone
can only be watched. A host from before the flags existed sets none of them. Recent devices, their
online state (ping/pong probes) and the LAN scan feed one merged device list on
Windows.

## 7. Data on disk

Everything lives in the user's Deskhub folder (`~/.deskhub`,
`%USERPROFILE%\.deskhub`): `host_key.pem` + `host_cert.pem` (identity),
`known_hosts` (hosts this machine trusts), `paired_devices` (machines this host
admits), `auth_salt` (non-secret verifier salt), `ui-settings.txt`,
`recent-devices.txt` (addresses + obscured passcodes), and per-run logs. File I/O
stays in `platform/`; the parsing and the data structures live in `core/` and are
unit-tested.

## 8. Testing

| Suite | Runs | Covers |
| --- | --- | --- |
| `make test` | offline, no sockets | all of `core/`: wire, framing, FEC, sessions, VT emulator, settings, strings, deterministic structured fuzzing |
| `make test-platform` | loopback sockets | real QUIC handshakes, SPAKE2 end-to-end, terminal host + viewer over the wire, PTY against a real shell, lockout, approval |
| `make test-integration` | loopback, fake capture/encode | full host↔client sessions: negotiation, video across the wire, input, passcode/approval gating, junk resistance |
| fuzz targets | nightly CI | parsers for wire, H.264, reassembly, terminal bytes, UI text |

CI additionally enforces clang-format (pinned version), clang-tidy, and ≥ 90 % line /
80 % branch coverage on `core/`.

## 9. Decisions worth remembering

- **The terminal link keeps itself alive and dials itself back**: a terminal viewer
  owns a QUIC connection of its own, separate from the video session, so none of the
  video path's keepalives reach it. Left alone at a prompt it carried no traffic at
  all and died on the 30 s QUIC idle timeout, and the viewer then stopped its thread
  in `Reattaching` without ever redialling — the shell was still waiting on the host
  for the full 2 minutes, and nothing went back for it. `TerminalViewer` now sends an
  ack-eliciting packet on a timer and redials with backoff, reusing
  `TerminalClient::Reattach()` (which was already written and tested in core, and
  simply never called) so the same shell comes back with its scrollback.
  `deskhub::KeepaliveIntervalUs` / `ReconnectDelayUs` hold the timings in core: the
  keepalive is at most half the idle timeout so one lost packet is survivable, and
  retrying stops exactly at `kTerminalReattachGraceUs`, because past that the host
  has already dropped the shell and reconnecting would silently open a new one.
- **An automatic share waits for the desktop instead of enumerating it once**:
  Windows registers autostart as a `ONLOGON` scheduled task, which fires before the
  session has a monitor to enumerate, so a single `ListDisplays()` at construction
  used to come back empty and the app reported that there was nothing to share.
  `deskhub::ui::AutoShareGate` (core, unit-tested) owns the retry rule — probe every
  `kAutoShareProbeMs`, give up after `kAutoShareGiveUpMs` — and every client drives it
  from its own timer, so the policy exists once. `NextAutoShareStep` is the same rule
  without the state, which is what the Swift client reaches through
  `dh_auto_share_step`. An automatic share never opens a modal: at login the window
  may be hidden in the tray, where a dialog is invisible and blocks the share
  forever, so refusals go to the Host banner and the log. The desktop clients also
  refresh their picker on the OS display-change signal, which is what keeps the list
  right when a monitor is plugged in later.
- **quiche over msquic/ngtcp2**: the only QUIC library with production evidence on
  both Android and iOS. It brings BoringSSL, which also serves SPAKE2 and the host
  identity — no second crypto library.
- **No connection migration**: no usable client-side support in any candidate
  library. Reconnect-and-reattach (tmux-style, already required for mobile
  backgrounding) covers it.
- **ECDSA P-256, not Ed25519**: BoringSSL's server side will not sign a TLS
  handshake with Ed25519 through quiche. Do not switch back. A stored Ed25519
  identity is replaced on load — it would fail every handshake as
  `QUICHE_ERR_TLS_FAIL` with nothing on screen to explain it.
- **The passcode verifier is one SHA-256, not an expensive KDF**: SPAKE2 already
  limits an attacker to one online guess per connection and leaves no transcript
  worth cracking offline, which is the whole job KDF hardness exists to do.
- **quiche is prebuilt, not FetchContent**: `scripts/build-quiche.sh` writes one
  directory per rust target under `third_party/quiche/` plus a shared `include/` —
  quiche.h and the BoringSSL headers boring-sys vendors, copied out because Deskhub
  calls BoringSSL directly for the host identity and wants one include path and no
  second TLS library. `DeskhubQuiche.cmake` turns that into `deskhub::quiche`; a
  missing library fails the configure.
- **Apple links `libplatform_bundled.a`**: the Xcode apps consume the platform
  archive from outside CMake, where a PRIVATE link to quiche never reaches their
  link line — so a `libtool` step fuses platform + quiche into the one archive the
  `.pbxproj` links.
- **The Windows toolchain traps are already cleared — keep them cleared**: quiche
  builds against the static CRT via `CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUSTFLAGS`
  for the Rust objects plus `/MT` in `CFLAGS_x86_64_pc_windows_msvc` for the
  BoringSSL objects (the msvc default is the DLL runtime, and forcing the flag
  through a blanket `RUSTFLAGS` broke the cargo build outright), and the whole tree
  pins `MultiThreaded` to match so the exe ships without the VC++ Redistributable;
  wxWidgets re-pins `wxBUILD_USE_STATIC_RUNTIME` on every configure because
  `wx_option()` caches it forever. BoringSSL must build under the default Visual
  Studio generator — the cmake crate only communicates /MT through per-config flags
  there, so forcing `CMAKE_GENERATOR=Ninja` silently reverts BoringSSL to /MD and
  the final link dies in LNK2038; if MSBuild trips MSB6003 on long paths, enable
  Windows long paths instead. Git Bash's `/usr/bin/link.exe` shadows the MSVC
  linker (put `cl.exe`'s directory first), its path rewriting mangles `/`-style
  arguments (`MSYS2_ARG_CONV_EXCL`), and NASM's installer does not touch PATH.
- **Android quiche skips cargo-ndk on Windows hosts**: cargo-ndk hands boring-sys an
  extension-less `clang` path, which CMake refuses on Windows, so `build-quiche.sh`
  sets `CC_*`/`CXX_*`/`AR_*`, the cargo linker and `--target=` for the ABI itself and
  calls plain cargo. BoringSSL still needs Ninja there (the Visual Studio generator
  cannot target the NDK), and bindgen picks up Visual Studio's libclang, which looks
  for `stddef.h` beside its own binary — `BINDGEN_EXTRA_CLANG_ARGS` points it at the
  NDK's resource headers with forward slashes, because bindgen splits that variable
  with shell rules and eats backslashes.
- **Every cross-compiled app builds its own quiche first**: `build-android`,
  `build-ios`, `build-macos` and `build-linux` depend on a quiche target for their
  ABIs, the way `debug`/`release` do for the host. quiche is per-ABI and the CMake
  configure fails without it, so a build that skipped this step looks like a broken
  toolchain rather than a missing library — and an app left behind at the last
  successful build speaks a protocol its peers no longer answer.
- **iOS quiche pins `IPHONEOS_DEPLOYMENT_TARGET=17.0`**: boring-sys's clang floats
  to the SDK default while rustc links for its own minimum, and the mismatch
  surfaces as an undefined `___chkstk_darwin` at link time.
- **Two clocks, deliberately**: `NowUs()` is monotonic (seconds of uptime) for
  intervals; `NowUnixSeconds()` is the only one that renders as a date. Mixing them
  is not loud — a stored monotonic stamp shows up as some time on 1 January 1970.
- **A Windows PTY child gets no standard handles**: with the host's own stdout
  redirected, Windows hands that redirection down past the pseudo-console attribute
  and the shell talks to the pipe; no handles at all sends it back to the attached
  ConPTY.
- **`wxWANTS_CHARS` on the Windows terminal grid**: without it the frame's dialog
  navigation eats Enter, Tab and the arrow keys before the terminal sees them.
- **macOS TCC pairs a grant with the code signature**: a locally built app.app
  (ad-hoc, re-signed every build) and the Developer ID dmg fight over the same
  `com.deskhub.macos` row — System Settings shows the permission granted while the
  copy just launched is denied, silently for Accessibility.
  `make reset-macos-permissions` clears every grant so the next launch asks again.
- **One static release CRT on Windows, every configuration**: cargo builds quiche
  against the static release CRT (the msvc default — never force it through
  `RUSTFLAGS`, that leaks into proc-macros and kills cargo), and the whole CMake
  tree pins `MultiThreaded` to match, which is also what keeps the app a single
  exe with no VC++ Redistributable. Rust offers no debug-CRT build, so Debug
  matches too: `_ITERATOR_DEBUG_LEVEL=0`, `/U_DEBUG`, `/RTC1` stripped — the
  release CRT has no `_CrtDbgReport` and no run-time check support. Any mismatch
  ends in a wall of LNK2038.
- **Passcode = self-service admission, approval = the fallback**: a typed code is
  always verified; no code means a human decides. The passcode never crosses the
  network in any form an attacker can take home.
- **The VT emulator is ours**: no platform terminal widget is available on all five
  clients under a usable licence, and owning it makes terminal behaviour testable
  offline and identical everywhere.
- **The host's shell mirror is fed from byte one**: PTY output is a destructive
  single-consumer stream — bytes read and shipped to the viewer cannot be replayed
  later — so the grid *Stop & attach* opens must be built as the bytes pass, not
  when the button is pressed. While a remote viewer is attached the mirror's own
  terminal-query responses are discarded: the viewer's screen already answers them,
  and the shell must not hear two answers.
- **One port**: beacon, screen and terminal share a single listener; QUIC
  multiplexes connections and streams. The old second port existed only because the
  pre-QUIC screen path monopolised the socket.
