# 10 — Web Client (WebTransport + WebCodecs)

The client runs directly in the browser, **view + send input only** (like Android v1 — no
host role yet). No installation; open a URL and it works.

Three pillars, each replacing exactly one platform-specific piece of the Windows client:

| Area | Windows | Web |
|------|---------|-----|
| Transport | `UdpSocket` (raw UDP) | **WebTransport** (QUIC datagram) |
| Decoding | `MfDecoder` + `Renderer` (D3D11) | **WebCodecs `VideoDecoder`** + canvas/WebGL |
| Protocol core | `core/` (C++20) | **`core/` compiled to WASM** (Emscripten) — reused as-is |

## 1. Why WebTransport, not WebSocket / WebRTC

Browsers **cannot open a raw UDP socket** — this is the root obstacle preventing the
current UDP protocol from talking directly to the web (see `04-protocol.md`). Three ways out:

| Option | Transport | Work required | Latency | Reuse of `core/` |
|-----------|-----------|---------------|--------|------------------|
| WebSocket | TCP | Host adds a WS server, sends whole frames, drops packetize/FEC (TCP is already reliable) | Good on LAN, poor under packet loss (HOL blocking) | Partial (transport dropped) |
| **WebTransport** ✅ | **QUIC datagram (UDP)** | Host adds a QUIC/HTTP3 server + certificate | Excellent, near native UDP | **Nearly complete** |
| WebRTC | RTP/SRTP | Remux H264→RTP, ICE/STUN/DTLS, SDP; throw away most of the custom control layer | Excellent | Very little |

**WebTransport was chosen** because **QUIC's unreliable datagrams map 1-to-1 onto the
current UDP datagram model**: `Packetizer` / `Reassembler` / XOR FEC / the session state
machine stay byte-identical. This is the decisive difference from WebRTC — WebRTC forces
repackaging H.264 into RTP and runs its own jitter buffer/NACK, so we would lose nearly all
of the protocol we designed. WebSocket would be simpler to build, but TCP head-of-line
blocking is exactly what section 6 of `01-architecture.md` rejected when choosing UDP.

**Browser compatibility (07/2026): WebTransport is now Baseline** — Chrome 97+, Edge 98+,
Firefox 114+, **Safari 26.4+**, Opera, Samsung Internet. At the time of the initial survey,
Safari still lacked WebTransport (the biggest risk of this option); it is now available, so
that risk is gone. WebCodecs is also widely available across the same set of browsers.

## 2. JS / WASM split — reusing `core/`

`core/` is already C++20 with no OS headers (the condition `core/CMakeLists.txt` imposes
so it builds with the NDK) — so **compile it to WASM with Emscripten and reuse
`Wire` / `Reassembler` / `ClientSession` / FEC exactly as Android does**. 1-to-1 mapping:

| Windows | Web | Role |
|---------|-----|---------|
| `UdpSocket.cpp` (winsock) | `wt-transport.js` (WebTransport) → WASM bridge | datagrams in/out |
| `MfDecoder` + `Renderer` | `video-decoder.js` (WebCodecs) + `<canvas>` | H.264 → screen |
| `InputCapture` (Raw Input) | `input.js` (Pointer Lock + KeyboardEvent) | mouse/keyboard capture |
| `MainMenuWindow` | `index.html` (address input field) | enter host + connect |
| preview window | `<canvas>` + HTML overlay | display + metrics |

The boundary is deliberately thin, wrapped in **one Embind/`EM_JS` bridge layer** (the
counterpart of `JniBridge.cpp` on Android): WASM holds all protocol logic; JS handles only
the three things exclusive to the browser — opening WebTransport, calling WebCodecs,
capturing input. **Not a single video frame has to be copied through the JS heap**:
datagrams arriving from WebTransport are written directly into WASM memory, the
`Reassembler` reassembles inside WASM, and the completed NAL is returned as a view and fed
into `VideoDecoder.decode()` — just one memory reference, no content copy.

> `Reassembler`/`Wire` could be rewritten in plain JS/TS to be lighter (no WASM needed).
> Rejected: that is precisely the hot path most prone to errors (fragment reassembly,
> deduplication, FEC recovery), and rewriting it opens a third implementation that must be
> kept in sync with Windows + Android. WASM reuses the exact code already backed by the
> `core_tests` test suite.

## 3. Transport constraint: everything goes over datagrams (v1)

WebTransport offers both **datagrams (unreliable)** and **streams (reliable, QUIC handles
retransmission)**. The temptation: push control (HELLO/START/REQUEST_KEYFRAME/RECONFIG) and
input onto reliable streams to **entirely drop** the 500ms-retry logic and the
repeat-send-against-stuck-keys logic — let QUIC handle reliability.

**v1 decision: send EVERYTHING over datagrams**, including control and input. Reason: that
is exactly what keeps `core/` unchanged, down to the line — `HostSession`/`ClientSession`/
`InputSender`/`InputReceiver` were built on the assumption "everything is an unreliable
datagram, handle retry yourself". Mixing in streams means branching the state machine by
transport. This is a **deferred optimization**: once the web client runs stably, moving
control+input onto reliable streams and trimming the repeat-send logic is an independent
improvement, not a prerequisite for a working build.

**Datagram size — a mandatory core change.** A QUIC datagram must fit within a single QUIC
packet; the usable payload is **smaller than 1200** because QUIC consumes packet headers +
a 16-byte AEAD tag. The browser reports the actual value via
`transport.datagrams.maxDatagramSize` (typically ~1180–1200 depending on the path).
Currently `kMaxVideoPayload` is a **compile-time constant of 1174** (see
`04-protocol.md` §6.1). It must be **turned into a runtime parameter** set at handshake from
`maxDatagramSize` — the packetizer splits NALs against this ceiling, the reassembler does
not need to know. The native UDP client passes the old ceiling; the web client passes the
ceiling QUIC reports. This is the **only core change** this approach requires.

> QUIC datagrams **are** subject to congestion control (RFC 9221: they count toward the
> congestion window, are **dropped** rather than queued under congestion, and are **not**
> retransmitted) — so `BitrateController` + the FEEDBACK channel keep their full value and
> are not made redundant by QUIC.

## 4. Video decoding: WebCodecs

`VideoDecoder` accepts H.264 NALs directly and decodes in hardware, producing `VideoFrame`s
drawn onto a `<canvas>` (WebGL or `drawImage`). It replaces both `MfDecoder` **and**
`Renderer` — WebCodecs outputs frames already in a displayable color space, no manual
NV12→BGRA step needed.

- **Configuration:** `codec: 'avc1.<profile><level>'` (e.g. `avc1.640028` for High). The
  stream is currently **Annex-B** (start code `00 00 00 01`); declare
  `avc: { format: 'annexb' }` to avoid remuxing to AVCC.
- **SPS/PPS:** NVENC enables `repeatSPSPPS`, so every IDR carries its parameters in-band —
  WebCodecs in annex-b mode consumes this directly, no separate `description` needed as
  with MSE.
- **Keyframe:** `EncodedVideoChunk({ type: 'key' | 'delta' })` — the IDR flag in the common
  header (`04-protocol.md` §2) maps straight to `type`. After packet loss when the
  `Reassembler` drops a frame → `ClientSession` requests REQUEST_KEYFRAME as before.
- **RECONFIG:** unlike `MfDecoder` (self-negotiating via `MF_E_TRANSFORM_STREAM_CHANGE`),
  this behaves like Android MediaCodec — `onReconfig` must call `decoder.configure()` again
  with the new size. The host sends an IDR alongside, so nothing is lost.
- **Latency:** set `optimizeForLatency: true` so the decoder does not batch multiple frames
  before output — the same spirit as `MF_LOW_LATENCY` on Windows.

## 5. Input: Pointer Lock + scancodes

Input capture in the browser is an area that matches the INPUT_EVENT design
(`04-protocol.md` §4.9) surprisingly well:

- **Relative mouse (FPS games):** the **Pointer Lock API** locks + hides the cursor, and
  `mousemove` yields `movementX/movementY` — exactly `dx/dy` with the `absolute=0` flag.
  This is the web version of the F9 mode. Absolute mouse (default): take canvas-relative
  coordinates normalized ×65535, `absolute=1`.
- **Keyboard — scancodes are mandatory.** Games read DirectInput/Raw Input by **scancode**,
  not vkCode (`07-input.md` §2). The browser provides `KeyboardEvent.code`
  (physical key, e.g. `"KeyW"`, `"ArrowUp"`) — layout-independent, mappable to Windows
  scancodes (including the E0 flag bit for extended keys, matching the `b` field).
  **`event.key` is unusable** (already layout-processed). A **`code` → PS/2 scancode lookup
  table** in JS is needed.
- **Stuck-key prevention:** keep the core's three layers (in-packet redundancy + idle-time
  replay + `ReleaseAll` on the host). Add one web-specific safety net: catch `blur` /
  `visibilitychange` events (tab switch) → emit key-up for every held key, because the
  browser does **not** send `keyup` when the window loses focus.
- **Browser limitations:** some combinations are swallowed by the OS/browser first
  (Cmd/Win, some F-keys, Esc exits Pointer Lock). Documented under limitations; cannot be
  bypassed from a normal web page.

## 6. TLS certificate + connection setup (the hardest part)

Browsers require WebTransport to run in a **secure context** with a valid certificate. This
app is peer-to-peer on a LAN, no domain name, no public CA — so use WebTransport's
**`serverCertificateHashes`** path, which allows self-signed certificates under strict
constraints (the same mechanism libp2p/LAN devices use):

- Certificate: **X.509v3**, key **ECDSA secp256r1 (NIST P-256)** — **not** RSA.
- Validity period **< 14 days** (prevents long-term tracking via the hash).
- Hash: **SHA-256** (the only algorithm the spec currently lists).

v1 flow:

1. The host generates a **temporary ECDSA P-256 certificate** (~13-day validity),
   self-signed, and computes its SHA-256. Rotate before expiry.
2. The host **prints / serves** the `ip:port` + hash pair — the same place users get the
   address to connect. Package it compactly as
   **one connection string** or a **QR code** for a single copy.
3. The web page opens:
   ```js
   new WebTransport(`https://${ip}:${port}/deskhub`, {
     serverCertificateHashes: [{ algorithm: 'sha-256', value: <ArrayBuffer 32B> }]
   })
   ```
   QUIC verifies the server certificate matches the pinned hash — a MITM swapping in a
   different certificate will fail the handshake.

**Where to serve the web page itself** (secure-context): the simplest and most robust
option is for the **host to serve the static web bundle via that same HTTP/3 server**. The
browser visits `https://<ip>:<port>/`, and the page and the WebTransport endpoint share
**the same origin and the same certificate** — one server, one port, one certificate. The
alternative (bundle hosted on an external CDN/artifact, opening only WebTransport to the
host) also works, but users would still have to enter the hash manually.

**v1 limitation (stated explicitly):** if the hash is obtained over an unauthenticated
channel (e.g. plaintext GET on the LAN), a man-in-the-middle can swap both the hash and the
certificate. Acceptable for a **trusted LAN**, as in earlier phases; the real solution is
out-of-band hash transfer (QR/manual copy) or an internal CA — grouped together with
"Encryption (DTLS/AEAD)" in `05-roadmap.md` Phase 6.

## 7. Host side: WebTransport server

To serve the web client, the host adds a **WebTransport server** (QUIC + HTTP/3, negotiated
via HTTP/3 CONNECT) alongside the existing UDP listener. It receives datagrams from
WebTransport and **feeds the same byte sequence into `HostSession`** as UDP does — the
session state machine does not distinguish transports.

This is an **`IHostTransport` binding shared by every host OS** (written once on msquic,
not per-OS like `UdpSocket`). The full design — interface, QUIC library comparison, module
layout, and **why only the web uses QUIC while native keeps UDP (hybrid)** — is in
**`11-platform-transport.md`** (§2 and §5). The QUIC library is finalized at web-M2 (§10).

The **web-specific** aspect of this server (versus an ordinary QUIC server): it both
**serves the static web bundle** and exposes the **WebTransport endpoint**, using **the
same certificate**, so that the web page and the WebTransport connection share an origin —
satisfying secure-context without a public certificate (§6).

## 8. Changes to `core/`

Almost none — that is the entire reason for choosing WebTransport. Exactly one mandatory
change:

- **`kMaxVideoPayload` goes from compile-time constant → runtime parameter.** Set at
  handshake from the `maxDatagramSize` the client reports (§3). The packetizer receives
  this ceiling as a parameter; the reassembler is unchanged. The native UDP client passes
  the old ceiling (1174), so Windows/Android behavior does not change.

Everything else (`Wire`, `Reassembler`, `HostSession`, `ClientSession`, FEC, `InputSender/
Receiver`) builds to WASM and runs unmodified.

## 9. Planned file structure

```
client/web/                 static web bundle (does not exist yet — created in this phase)
  index.html                host input field + canvas + metrics overlay
  src/
    main.js                 lifecycle: connect → receive datagrams → decode → render
    wt-transport.js         WebTransport: open, send/receive datagrams, read maxDatagramSize
    video-decoder.js        WebCodecs VideoDecoder + canvas rendering
    input.js                Pointer Lock + code→scancode table + send INPUT_EVENT
    core-bridge.js          Embind/EM_JS bridge into core.wasm
  wasm/                      Emscripten output of core/ (core.js + core.wasm)

core/                       only the payload ceiling changed to runtime (see §8)
(host module `WebTransportHost` — shared by every OS, layout in 11-platform-transport.md §3)
```

`core/CMakeLists.txt` needs an additional **Emscripten** toolchain branch (parallel to NDK)
to produce `core.wasm`. `client/web` builds with a lightweight bundler or plain ES modules
— not yet decided, deferred to M1. On the host side, `WebTransportHost` lives in the shared
host module (not in `core/`); see `11-platform-transport.md` §3.

## 10. Roadmap / verification milestones

- ⬜ **M1 — WASM + in-tab loopback.** Build `core/` to WASM; a test page feeds fake
  datagrams (dumped from `core_tests`) into the WASM `Reassembler` → NAL → `VideoDecoder` →
  canvas. Proves the WASM↔WebCodecs chain works, **no network needed yet**. The counterpart
  of "Phase 2 loopback".
- ⬜ **M2 — WebTransport echo + certificate.** An msquic host serves the bundle + the
  WebTransport endpoint on a temporary ECDSA certificate; the browser connects via
  `serverCertificateHashes`, and the HELLO/HELLO_ACK handshake completes a full round trip
  over datagrams. The QUIC library is finalized here.
- ⬜ **M3 — Real video e2e on LAN.** Host shares a display → the web client displays it;
  measure fps/kbps/packet loss/RTT/e2e latency like the Windows overlay. Compare latency
  milestones against native UDP.
- ⬜ **M4 — Input.** Pointer Lock + scancode keyboard controlling a regular application and
  then a real game; measure input latency. Enable the `blur`/`visibilitychange` safety net.

## 11. Risks / open questions

1. **Certificate hash distribution** (high — UX): how users obtain the hash conveniently
   and safely. Decide between connection-string/QR (out-of-band) and host-self-serving
   (convenient, but the hash travels over the LAN channel). See §6.
2. **msquic's level of WebTransport support** (medium): must be confirmed at M2; if
   lacking, consider quiche.
3. **Datagram ceiling adjustment** (low): `maxDatagramSize` varies with the path; decide
   between taking the value at handshake and fixing it for the session, or updating
   dynamically on RECONFIG.
4. **core.wasm size/performance** (low): the protocol logic is lightweight, not a pixel hot
   path — expected to be a non-issue, but measure at M1.
5. **Keys swallowed by the browser/OS** (low): some combinations cannot be captured from a
   normal web page (§5). Acceptable for v1 view+input.

## 12. Why this design (summary)

- **WebTransport datagrams** because it is the only browser transport that maps 1-to-1 onto
  the current UDP model → preserves all of `core/` and the entire v1 protocol design.
- **Everything over datagrams in v1** because that is the condition for `core/` to remain
  unchanged; moving control/input onto reliable streams is a deferred optimization.
- **WASM reuse of core** instead of a JS rewrite — the same reason Android reuses it: do
  not open a third implementation of the most error-prone hot path.
- **WebCodecs** because it decodes in hardware and outputs displayable frames, merging the
  decoder + renderer roles into one.
- **`serverCertificateHashes`** because the app is a LAN peer with no public CA — exactly
  the use case this branch of WebTransport was created to serve.
