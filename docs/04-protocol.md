# 04 — Wire Protocol

Specification of the v1 wire protocol between a Deskhub host (Agent) and its clients.

This document and `core/include/deskhub/protocol/Wire.h` are two forms of the same
specification. `Wire.h` (with `core/src/protocol/Wire.cpp`) is the **single implementation**
of every byte layout described here — no other module in the project is allowed to read
or write raw datagram bytes — and its header comment names this file as the source of
truth. **The two must change together**: a change to either one that is not mirrored in
the other creates two conflicting specifications. Round-trip and malformed-input tests
for every message live in `core/tests/protocol/WireTests.cpp`.

Related documents: 01-architecture.md (where the wire layer sits in the system),
06-transport.md (video transport policy in depth), 07-input.md (input pipeline),
11-platform-transport.md (sockets, ports, and per-platform plumbing).

## 1. Overview

- **Transport**: UDP, one single port. All traffic — control, video, input — is
  multiplexed on that port via the `chan` byte of the common header. The host port is
  **always 47777** — a fixed platform-level constant (`kDeskhubPort` in each platform's
  `net/UdpSocket.h`), not a default: there is no setting for it, clients type a bare IP,
  and a busy port is a hard error rather than a walk to the next one (changed 2026-07-27;
  see 11-platform-transport.md). The core protocol code never opens sockets; bytes enter
  through `HandlePacket`/`Reply` and leave through `send` callbacks.
- **Byte order**: every multi-byte integer field is **big-endian** (network byte
  order). The only code allowed to know this is `core/include/deskhub/protocol/ByteOrder.h`
  (`PutU16/PutU32/PutU64`, `GetU16/GetU32/GetU64`); `Wire.cpp` calls down into it.
  Byte-by-byte shifts are used deliberately — no pointer casts, so unaligned payloads
  are safe on ARM.
- **Version**: `kProtocolVersion = 1`. See §8 for the enforcement rules.
- **Datagram size**: `kMaxDatagram = 1200` bytes — an Internet-safe MTU that avoids IP
  fragmentation. No message ever exceeds it; variable-length messages truncate their
  contents to guarantee it.
- **Time unit**: microseconds, carried as `u64` (`timestampUs`, `sendTimeUs`,
  `timebaseUs`). Microseconds overflow 32 bits after ~71 minutes, hence 64-bit
  everywhere on the wire. Host and client clocks are **not** synchronized; §5.4
  describes how the client relates them.
- **Reliability**: there is no per-message ACK layer. Messages that must arrive are
  simply repeated by the sender (HELLO, START, REQUEST_KEYFRAME, SET_FOCUS, input
  redundancy); everything else is best-effort.
- **API convention** (`Wire.h`): `Build*(out, ...)` writes one complete datagram
  (common header + payload) and returns the byte count, or `0` if `out` is too small —
  it never overruns. `Parse*` returns `std::optional`/`0` for short or malformed input;
  every declared length coming off the network is checked against the real buffer
  bounds before it is read. This is the trust boundary of the whole program.

## 2. Common header

Every datagram starts with the same 8-byte header (`kCommonHeaderSize = 8`,
`struct CommonHeader`, built by `WriteCommon` in `Wire.cpp`):

```
 0        1        2        3        4        5        6        7
+--------+--------+--------+--------+--------+--------+--------+--------+
| ver    | type   | flags  | chan   | sessionId (u32, big-endian)       |
+--------+--------+--------+--------+--------+--------+--------+--------+
```

| Field | Size | Meaning |
|-------|------|---------|
| `ver` | u8 | Protocol version. Must equal `kProtocolVersion` (1); `ParseCommonHeader` rejects anything else. |
| `type` | u8 | Message type (`MsgType`, table in §3). Unknown values are not rejected at parse time — receivers `switch` on the type and silently ignore unrecognized ones, so a later version can add types without breaking old peers. |
| `flags` | u8 | Type-specific flags. Used by VIDEO_PACKET/FEC_PACKET (`kVideoFlagIdr`, `kVideoFlagFrameEnd`); 0 elsewhere. |
| `chan` | u8 | Logical channel (`Chan`): `0 = Control`, `1 = Video`, `2 = Input`, `3 = Audio` (reserved — no audio messages exist in v1). |
| `sessionId` | u32 | Session identifier. `0` in every pre-session message (HELLO, HELLO_ACK, LIST_SOURCES, SOURCE_LIST, and probe PINGs); assigned by the host in HELLO_ACK for everything after. |

`PayloadOf` returns the bytes after the header (empty if the datagram is shorter than
8 bytes).

### 2.1 Session identification and peer migration

A session is identified by **sessionId, not by source addr:port**:

- `HostSession::HandlePacket` returns `true` only for a valid packet that belongs to
  the current session; the caller (the platform Agent loop) then updates its peer
  address from the datagram's source address. A mobile client that switches networks
  (Wi-Fi ↔ LTE) keeps its session as long as it keeps its sessionId. BYE deliberately
  returns `false` even though it is a valid packet — the session just closed, so the
  peer address must not be updated from it.
- The sessionId is generated by `HostSession::BeginSession` from the platform CSPRNG
  (`HostCallbacks::randomBytes`). If no entropy is available the host **fails closed**:
  it rejects the connection rather than fall back to a guessable id, because the
  sessionId is the only fence between "my client" and the rest of the network.
- `sessionId = 0` never authorizes anything. `HostSession::InSession` requires the
  current id to be non-zero **and** equal — otherwise a forged `START` with
  sessionId 0, sent while the host is mid-handshake (when its own id is still 0),
  would push it straight into streaming.
- Every valid in-session packet, whatever its type, **feeds the session timeout**
  (`lastRecvUs`); see §5.5.

## 3. Message types

All `MsgType` values in `Wire.h`. Direction: C = client, H = host (Agent).

| Type | Name | Channel | Direction | Reliability / repetition |
|------|------|---------|-----------|--------------------------|
| 0x01 | HELLO | Control | C→H | Repeated every 500 ms until HELLO_ACK; give up after 10 s (`kHelloRetryUs`, `kHelloGiveUpUs`). |
| 0x02 | HELLO_ACK | Control | H→C | Sent in reply to every HELLO (including repeats). Also the reject vehicle (`codec = Rejected`). |
| 0x03 | START | Control | C→H | Repeated every 500 ms until the first video packet arrives (the only proof the host received it). |
| 0x04 | BYE | Control | both | Sent once, best-effort, on orderly shutdown. |
| 0x05 | LIST_SOURCES | Control | C→H | Best-effort request; client re-asks on its own schedule. Pre-session (sessionId 0), answered by `Beacon`. |
| 0x06 | SOURCE_LIST | Control | H→C | Best-effort reply. Single 6-byte record layout (§4.6). |
| 0x10 | VIDEO_PACKET | Video | H→C | Unreliable; protected by FEC (§6.3) and NACK retransmission (§6.4). |
| 0x11 | FEC_PACKET | Video | H→C | Unreliable parity; only present when FEC is enabled. |
| 0x20 | INPUT_EVENT | Input | C→H | "Lightly reliable": each event is sent ~3× via redundancy tails and repeats; the receiver deduplicates by sequence number (§4.9). |
| 0x30 | PING | Control | C→H | Every 1 s in-session (`kPingIntervalUs`); also pre-session probes with sessionId 0 (answered by `Beacon`). |
| 0x31 | PONG | Control | H→C | Verbatim echo of the PING payload. |
| 0x32 | FEEDBACK | Control | C→H | Periodic (~1 s), best-effort. Input to `BitrateController`. |
| 0x33 | REQUEST_KEYFRAME | Control | C→H | Repeated every 250 ms (`kKeyframeRetryUs`) while an IDR is wanted. |
| 0x34 | RECONFIG | Control | H→C | Best-effort; host follows it with an IDR so the decoder resyncs. |
| 0x35 | SET_FOCUS | Control | C→H | Event-driven, sent `kFocusRepeats = 3` times, 50 ms apart (`kFocusRetryUs`). Never periodic. Host acts only on `false` (release held keys, §4.13). |
| 0x36 | NACK | Control | C→H | Best-effort; self-throttled (§6.4). |
| 0x37 | INVALIDATE_REF | Control | C→H | Best-effort, sent once per abandoned frame. |

`Chan::Audio` (3) is reserved: no message type uses it in v1. Values 0x07–0x0A were
DISCOVER/ANNOUNCE/AUTH_CHALLENGE/AUTH_RESPONSE and 0x38 was CLIPBOARD — all removed
2026-07-27 (LAN discovery, the auth layer, and clipboard sync); the values stay
unassigned. Messages with no payload
(START, BYE, LIST_SOURCES, REQUEST_KEYFRAME) consist of the common header alone
(`BuildEmpty` in `Wire.cpp`).

## 4. Payload layouts

All offsets below are relative to the start of the payload (byte 8 of the datagram).
All integers big-endian. "Tail-appended" fields follow the compatibility pattern of
§8: old parsers stop early, new parsers tolerate their absence.

### 4.1 HELLO (0x01) — client capabilities

`struct Hello`, `BuildHello`/`ParseHello`. sessionId = 0 (the session does not exist
yet — HELLO_ACK creates it).

```
off  size  field
 0    4    clientId        client-chosen identifier (random per session)
 4    2    codecMask       bit0 = H.264 (kCodecMaskH264), bit1 = HEVC, bit2 = AV1
 6    2    maxWidth
 8    2    maxHeight
10    1    desiredFps
11    2    features        reserved feature bits
13    1    sourceId        source to view (from SOURCE_LIST; 0 = first source)
```

Fixed 14 bytes. Compatibility: `ParseHello` accepts a 13-byte payload
(pre-`sourceId` clients, `sourceId` defaults to 0). The Phase-10 tail
(deviceToken/deviceName) was removed 2026-07-27 with the auth layer.

### 4.2 HELLO_ACK (0x02) — session grant or rejection

`struct HelloAck`, `BuildHelloAck`/`ParseHelloAck`. sessionId in the **payload**, not
the header — when this message is sent the client cannot yet validate a header field.

```
off  size  field
 0    4    sessionId       CSPRNG-assigned; 0 when rejecting
 4    1    codec           Codec: 0 = H264, 1 = Hevc, 2 = Av1, 0xFF = Rejected
 5    2    width           negotiated stream size
 7    2    height
 9    1    fps
10    4    bitrateBps      starting bitrate
14    8    timebaseUs      host clock at the moment the ACK was built (µs) — seeds
                           the client's e2e-latency offset estimate (§5.4)
--- tail-appended (Phase 9) ---
22    2    reserved        Always written as 0, always ignored on read. This used to be
                           `flags`: bit0 = kAckFlagInputAccepted, bit1 = clipboard.
                           Clipboard went 2026-07-27, the input flag went with it the
                           same day (input is always accepted now). The two bytes STAY
                           so `reason` keeps offset 24 — dropping them would make every
                           already-released build read the reject reason as flags.
--- tail-appended ---
24    1    reason          RejectReason, meaningful only when codec = Rejected:
                           0 None, 1 Busy, 2 CodecMismatch. Out-of-range values are
                           read as None. (Auth reasons 3–5 removed 2026-07-27.)
```

A rejection is a HELLO_ACK with `codec = Codec::Rejected` and all other fixed fields
zero (`HostSession::SendReject`) — reusing the message the client is already waiting
for gives it an immediate, definitive answer instead of a 10-second timeout.

### 4.3–4.4 (removed)

AUTH_CHALLENGE (0x09) / AUTH_RESPONSE (0x0A) — the PBKDF2 challenge–response
handshake — were removed 2026-07-27 together with the whole auth layer (trusted-LAN
decision, see 15-review-todo.md §A1). Neither the handshake nor the session is
encrypted; do not expose the host port to untrusted networks.

### 4.5 START (0x03), BYE (0x04), LIST_SOURCES (0x05), REQUEST_KEYFRAME (0x33)

Empty payloads — the common header carries the whole meaning. START/BYE/
REQUEST_KEYFRAME carry the real sessionId; LIST_SOURCES carries 0 (the client asks
before it has a session, because it needs the list to pick a `sourceId` for HELLO).

### 4.6 SOURCE_LIST (0x06) — host→client, sessionId 0

`BuildSourceList`/`ParseSourceList`. The only message with variable-length records.
At most `kMaxSources = 8` entries; names truncated to `kMaxSourceNameBytes = 64` on a
UTF-8 character boundary (`Utf8TruncLen`).

```
payload: count(u8), then count records:
  sourceId(u8) width(u16) height(u16) nameLen(u8) name(nameLen)
```

Each record is 6 fixed bytes plus the name. Every source is a display — the host
shares whole monitors only, one `sourceId` per monitor. (A per-record `kind` byte
plus the `kSourceListFlagKind` header flag used to distinguish windows from
displays; window sharing and the kind byte were removed 2026-07-27, leaving this
single layout.) The declared `count` is untrusted: parsing clamps to the output
span and stops at the first record that would cross the real payload boundary.

### 4.7 The pre-session Beacon (DISCOVER/ANNOUNCE removed)

DISCOVER (0x07) / ANNOUNCE (0x08) — the LAN broadcast discovery pair — and the
client-side `HostRegistry` were removed 2026-07-27; connections start from a typed
address. What remains is `deskhub::Beacon` (`deskhub/discovery/Beacon.h`) on the
host: it answers LIST_SOURCES (even with an empty list) and PING with sessionId 0
(liveness/RTT probe); a PING with a non-zero sessionId is session business and is
left to `HostSession`. The Beacon builds the reply, the caller `sendto`s it back to
the datagram's source address.

### 4.8 VIDEO_PACKET (0x10) and FEC_PACKET (0x11) — Video channel

Both carry a 16-byte sub-header at the start of the payload. Flags ride in the
**common header's** `flags` byte: `kVideoFlagIdr` (bit 0) and, for video only,
`kVideoFlagFrameEnd` (bit 1, set on the last fragment of a frame).

```
VIDEO_PACKET payload (kVideoHeaderSize = 16, struct VideoHeader):
 0    4    frameId       monotonically increasing per frame
 4    8    timestampUs   host capture clock (µs)
12    2    pktIndex      0-based fragment index
14    2    pktCount      total fragments of this frame
16    …    fragment      1..kMaxVideoPayload (= 1174) bytes of Annex-B H.264

FEC_PACKET payload (kFecHeaderSize = 16, struct FecHeader):
 0    4    frameId
 4    8    timestampUs
12    2    pktCount      fragment count of the frame this parity covers
14    1    groupIndex    interleaved group number (u8 — hence kMaxFecGroups = 256)
15    1    reserved      written as 0
16    2    lenXor        kFecLenPrefix: XOR of the covered fragments' lengths (u16)
18    …    parityData    XOR of the covered fragments, zero-padded to the group's
                         longest fragment; ≤ kMaxVideoPayload bytes
```

`kMaxVideoPayload = kMaxDatagram − kCommonHeaderSize − kFecHeaderSize − kFecLenPrefix
= 1174`: the FEC packet is the tighter of the two, so it sets the bound for both.
Parsing rejects, as forged: `pktCount == 0`, `pktIndex >= pktCount`, oversized
payloads (which would otherwise overflow the fixed-width parity buffer during XOR
recovery), and `groupIndex >= ceil(pktCount / kFecGroupSize)`. See §6 for semantics.

### 4.9 INPUT_EVENT (0x20) — Input channel, client→host

`BuildInputEvents`/`ParseInputEvents`. One datagram carries a **batch** of events
(mouse movement produces hundreds per second; one 8-byte header per 19-byte event
would be waste).

```
payload header (kInputHeaderSize = 5):
 0    4    firstSeq      sequence number of events[0]
 4    1    count         1..kMaxInputEvents (= 62)

then count × event (kInputEventSize = 19):
 0    1    evType        InputType (below)
 1    8    timestampUs   client clock at capture
 9    4    a             int32 sent as its u32 bit pattern
13    4    b             int32 sent as its u32 bit pattern
17    1    state         1 = press/hold, 0 = release; ignored by MouseMove/MouseWheel
18    1    absolute      1 = a/b are normalized absolute coordinates (MouseMove)
```

Event `i` implicitly carries `seq = firstSeq + i` — the receiver
(`deskhub/input/InputReceiver.h`) relies on exactly this to deduplicate. `InputType`
and field meanings (`Wire.h`):

| evType | Kind | `a` | `b` |
|--------|------|-----|-----|
| 1 | Key | Windows virtual-key code (VK) | scancode in the low 8 bits; bit 8 (`kScanExtended = 0x100`) = E0 extended key (arrows, right Ctrl, …). `b = 0` means "no scancode": the host looks it up from the VK against **its own** keyboard layout (required for Raw Input/DirectInput games). |
| 2 | MouseMove | `absolute=1`: x normalized 0..65535 across the client rect; `absolute=0`: raw dx | `absolute=1`: y normalized 0..65535; `absolute=0`: raw dy |
| 3 | MouseButton | `MouseButton`: 1 Left, 2 Right, 3 Middle, 4 X1, 5 X2 | 0 |
| 4 | MouseWheel | 0 | delta, multiples of 120; `state` ignored |

The key-code space is the Windows VK space. Clients without physical keyboards
translate typed characters via `deskhub/input/KeyMap.h` (`CharToKeyChord`): letters and
digits map to their VK directly, symbols map to `VK_OEM_*` codes assuming a **US host
layout** (a deliberate v1 limitation), always with `b = 0`.

**Reliability policy** (`deskhub/input/InputSender.h`): state-changing events
(`IsStateEvent`: Key, MouseButton) are the ones whose loss causes stuck keys, so every
datagram appends a redundancy tail of the last `kInputRedundancy = 8` already-sent
events, new events go out in batches of at most `kInputBatchMax = 24`, and after the
queue drains the tail is re-sent `kInputRepeatCount = 2` more times at
`kInputRepeatIntervalUs = 25 ms` intervals — each event crosses the wire ~3 times in
~50 ms. The receiver keeps a single `lastAppliedSeq` watermark: any event with
`seq <= lastAppliedSeq` is dropped (duplicate or stale), anything newer is applied in
order. There is no reordering buffer — a late input event is worse than none.

### 4.10 PING (0x30) / PONG (0x31)

`struct PingPong`, `BuildPing`/`BuildPong`/`ParsePingPong`. Identical 12-byte
payloads; PONG is a verbatim echo.

```
 0    4    pingId        client counter
 4    8    sendTimeUs    CLIENT clock at send; echoed untouched, so
                         RTT = now − sendTimeUs with no clock sync and no lookup table
```

### 4.11 FEEDBACK (0x32) — client→host link report

`struct Feedback`, `BuildFeedback`/`ParseFeedback`. Built once per ~1-second window by
`MakeFeedback` (`deskhub/control/LinkStats.h`) and sent even when clean — silence would
read as disconnection, and the host needs a "link is clear" signal before raising the
bitrate.

```
 0    2    lostFrames        frames dropped in the window
 2    1    lossPct           data-packet loss %, rounded (parity packets excluded
                             from the denominator)
 3    2    rttMs             last measured RTT, ms
 5    4    recvBitrateKbps   video bitrate actually received
```

### 4.12 RECONFIG (0x34) — host→client mid-session change

`struct Reconfig`, `BuildReconfig`/`ParseReconfig`. Sent when the source resizes or
the bitrate is re-negotiated; the host follows with an IDR. The client ignores a zero
width/height or bitrate rather than reconfigure to nonsense.

```
 0    2    width
 2    2    height
 4    4    bitrateBps
```

### 4.13 SET_FOCUS (0x35) — client→host

`BuildSetFocus`/`ParseSetFocus`. One payload byte: `1` = this source's preview just
gained focus — the host takes **no action** (it used to raise the shared window to
the foreground; that behavior went with window sharing, removed 2026-07-27);
`0` = focus left — the host releases any held keys for this session, so a client
that backgrounds mid-keypress cannot leave a key stuck down. Sent on **events
only**, 3× with 50 ms spacing.

### 4.14 NACK (0x36) — client→host retransmit request

`BuildNack`/`ParseNack`.

```
 0    4    frameId
 4    1    count          1..kMaxNackIndices (= 593)
 5    …    count × pktIndex(u16)   missing fragment indices
```

(`kNackHeaderSize = 5`.) Parse rejects `count = 0` and a `count` that overstates the
payload, and clamps to the caller's output capacity. Semantics in §6.4.

### 4.15 INVALIDATE_REF (0x37) — client→host

`BuildInvalidateRef`/`ParseInvalidateRef`. Payload: `frameId(u32)`. "I have abandoned
this frame entirely — stop using it as a reference." Lets an encoder that supports
reference invalidation recover with a cheap P-frame instead of a full IDR; a host
whose encoder cannot do it falls back to forcing an IDR.

### 4.16 (removed)

CLIPBOARD (0x38) — two-way chunked clipboard-text sync (GĐ8) — was removed
2026-07-27 together with `ClipboardAssembler` and every client's pasteboard wiring.

## 5. Protocol flows

State machines: `deskhub/session/HostSession.h` (Idle → Ready →
Streaming) and `deskhub/session/ClientSession.h` (Idle → Hello → Starting →
Streaming → Dead).

### 5.1 Connection

```
client                                                        host (Agent)
  |                                                                |
  |-- LIST_SOURCES ----------------------------------------------->|   optional:
  |<--------------------------------- SOURCE_LIST (sources) -------|   pick sourceId
  |                                                                |
  |-- HELLO (clientId, codecMask, sourceId) ---------------------->|
  |        repeated every 500 ms; give up after 10 s               |
  |<-------------------- HELLO_ACK (sessionId, params, flags) -----|  IDLE → READY
  |                                                                |
  |-- START (sessionId) ------------------------------------------>|  READY → STREAMING,
  |        repeated every 500 ms until video arrives               |  onStart forces an IDR
  |<===================== VIDEO_PACKET / FEC_PACKET ==============>|  client: Starting →
  |                                                                |  Streaming on first
  |   steady state: PING 1/s, FEEDBACK ~1/s, INPUT_EVENT,          |  video packet
  |   SET_FOCUS, NACK, REQUEST_KEYFRAME as needed                  |
  |-- BYE / <-- BYE ---------------------------------------------- |  either side, once
```

There is **no authentication step**: the auth layer (PBKDF2 challenge–response,
trusted-device tokens, lockout) was removed 2026-07-27 — the app targets trusted
LANs. Rules, exactly as implemented:

- **HELLO handling** (`HostSession::HandlePacket`): a HELLO from a different clientId
  while READY/STREAMING is rejected with `Busy` (v1 serves one client per session). A
  client without H.264 in `codecMask` is rejected with `CodecMismatch` (v1 streams
  H.264 only). A repeated HELLO from the current client just re-sends HELLO_ACK.
- **Entropy fail-closed**: if `randomBytes` fails, the host refuses to issue a
  sessionId — it rejects the session rather than proceed with a predictable value
  (the random sessionId is the only fence against blind packet forgery on UDP).

### 5.2 IDR on demand

The encoder uses an infinite GOP — no periodic IDRs. When the `Reassembler` drops a
frame (§6.2) it swallows all further non-IDR frames and raises a loss event; the
client then holds `RequestKeyframe`, and `ClientSession::Tick` sends REQUEST_KEYFRAME
every `kKeyframeRetryUs = 250 ms` until the request is cancelled (an IDR arrived).
The host forwards each one to the encoder via `onKeyframeRequest`. The same mechanism
covers joining mid-stream: the Reassembler starts in the waiting-for-IDR state.

### 5.3 Retransmission (NACK)

See §6.4 for the transport policy; on the wire it is: client sends NACK (§4.14), host
looks each `(frameId, pktIndex)` up in `deskhub/transport/RetransmitCache.h` (verbatim
copies of the last `kCacheFrames = 8` frames' video datagrams) and re-sends exactly
those datagrams unchanged.

### 5.4 Clock relation (RTT and end-to-end latency)

Two mechanisms, both deliberately avoiding clock synchronization:

- **RTT**: PING carries the client's own `sendTimeUs`; PONG echoes it verbatim, so
  `rtt = now − sendTimeUs` is a pure client-side subtraction.
- **End-to-end latency estimate** (inline in each ClientLoop): video timestamps are
  host-clock. The client records `ackDelta = local clock − HelloAck::timebaseUs` at
  HELLO_ACK, subtracts `minRTT/2` to approximate the clock offset, and reads
  `e2e = now − offset − frame pts` when a frame is presented. This is an estimate
  assuming a symmetric path, not a measurement; it is display-only and never travels
  on the wire.

### 5.5 Disconnect and timeouts

| Constant | Value | Where | Meaning |
|----------|-------|-------|---------|
| `kHelloRetryUs` | 500 ms | ClientSession.h | HELLO and START repeat interval. |
| `kHelloGiveUpUs` | 10 s | ClientSession.h | Give up connecting if the host stays silent. |
| `kPingIntervalUs` | 1 s | ClientSession.h | In-session PING cadence; PINGs are what keep an idle session alive. |
| `kSessionTimeoutUs` | 5 s | HostSession.h (shared by both sides) | No valid in-session packet for 5 s → host returns to Idle (`onDisconnect` — the caller must release any held keys), client goes Dead. |
| `kKeyframeRetryUs` | 250 ms | ClientSession.h | REQUEST_KEYFRAME repeat interval. |
| `kFocusRetryUs` / `kFocusRepeats` | 50 ms / 3 | ClientSession.h | SET_FOCUS burst. |

BYE is sent once, best-effort, by whichever side leaves first; the other side treats
it as an immediate disconnect. Loss of BYE simply degrades to the 5-second timeout.
Every valid in-session packet of any type feeds the timeout on both sides.

## 6. Video channel

Host side: `deskhub/transport/Packetizer.h`; client side:
`deskhub/transport/Reassembler.h`. Full policy discussion in 06-transport.md.

### 6.1 Fragmentation

`Packetizer::SendFrame` splits one encoded frame into
`pktCount = ceil(size / kMaxVideoPayload)` fragments. Every fragment except the last
carries **exactly** `kMaxVideoPayload = 1174` bytes, so the receiver derives each
fragment's byte offset from `pktIndex` alone — there is no offset field on the wire.
The last fragment carries the remainder and the `kVideoFlagFrameEnd` flag; IDR frames
carry `kVideoFlagIdr` on every fragment. Frames needing more than 65535 fragments are
not sendable (`pktIndex`/`pktCount` are u16; `SendFrame` returns 0).

### 6.2 Reassembly and drop policy

The `Reassembler` holds at most `kMaxPendingFrames = 4` frames under assembly and
releases frames strictly in `frameId` order (H.264 inter-prediction makes
out-of-order decode produce garbage). A frame is dropped when:

- **Timeout** — incomplete for more than 2 frame intervals after its first fragment
  arrived (frame interval from the negotiated fps, default 16 667 µs), or
- **Overtaken** — ≥ 2 newer frames have completed past it, or
- **Evicted** — the pending queue is full.

Fragments of already-released or already-dropped frames are discarded (a barrier
frameId enforces this). After any loss-drop (and on join) the Reassembler swallows
complete non-IDR frames (`PreIdr` drops) until an IDR arrives, and raises a one-shot
loss event that drives §5.2.

### 6.3 FEC scheme (interleaved XOR parity)

Enabled per-frame by the host (`Packetizer::SetFecEnabled`, driven by
`BitrateController`); overhead is 1/`kFecGroupSize` = 1/8 of video bandwidth.

- A frame of `pktCount` fragments is divided into
  `numGroups = ceil(pktCount / kFecGroupSize)` **interleaved** groups
  (`kFecGroupSize = 8`): fragment `i` belongs to group `i mod numGroups` — not
  consecutive runs. Group membership is derived from `pktCount`, costing no wire
  bytes.
- One FEC_PACKET per group, sent **after** all data fragments. Its parity payload is
  `lenXor` (u16, XOR of the member fragments' lengths — needed because the frame's
  last fragment is shorter) followed by the XOR of the member fragments' bytes,
  zero-padded.
- Recovery (`Reassembler::PushFec`/`TryRecover`): a group missing **exactly one**
  fragment is repaired by XOR-ing the parity with the received members — length first,
  then bytes. Because same-group fragments sit `numGroups` positions apart, a burst of
  up to `numGroups` **consecutive** losses touches each group at most once and is
  fully recoverable; ≥ 2 losses in the same group are not (one equation, two
  unknowns). Parity arriving before a reordered data fragment is retained. A
  one-fragment frame's parity is a full copy of that fragment, so `timestampUs`,
  `pktCount`, and the IDR flag in the FEC header suffice to rebuild the frame from
  parity alone.
- `groupIndex` is u8, so frames needing more than `kMaxFecGroups = 256` groups are
  sent without FEC.

### 6.4 NACK retransmission

Complements FEC: FEC repairs single losses per group with zero added latency, NACK
repairs everything else when the RTT allows. `Reassembler::PlanNack` lists the missing
`pktIndex` values of the **oldest** incomplete pending frame, with self-throttling:

- no NACK before `kNackHoldUs = 2 ms` after the frame's first fragment (give
  reordered packets a beat before declaring them lost);
- no NACK for a frame already past its 2-frame-interval assembly deadline
  (a retransmit could not arrive in time);
- no repeat NACK for the same frame within `max(rttUs, kNackMinIntervalUs = 10 ms)` —
  a retransmitted packet needs ~1 RTT to arrive.

The host answers from `RetransmitCache` (last `kCacheFrames = 8` frames ≈ 0.13 s at
60 fps, longer than the client's assembly deadline; only VIDEO_PACKETs are cached —
parity is reconstructible and control has its own repetition). When the client instead
gives up on a frame it sends INVALIDATE_REF (§4.15).

### 6.5 Bitrate/FEC control loop

`deskhub/control/BitrateController.h`, fed by FEEDBACK once per second. Exact rules
(`BitrateController::Update`): FEC turns **on** at `lossPct ≥ 1 %` and **off** after
5 consecutive clean feedbacks. Bitrate: `≥ 5 %` loss → ×0.75; `≥ 2 %` → ×0.90;
`≤ 1 %` and ≥ 2 s since the last decrease → +5 % of the ceiling per feedback; results
clamped to `[min, startBps]` (the user-chosen start rate is also the ceiling), and
changes under 2 % of the current rate are suppressed to avoid churning the encoder's
rate control. The 1–2 % band deliberately enables FEC without lowering picture
quality. Applied changes reach the client as RECONFIG (§4.12).

## 7. Compatibility rules

All enforced in `Wire.cpp`; new code must follow the same patterns.

1. **Version byte is a hard gate.** `ParseCommonHeader` rejects any datagram whose
   first byte is not `kProtocolVersion` (1). There is no version negotiation in v1.
2. **Unknown message types are ignored, not errors** — receivers switch on `type` and
   fall through on `default`, so new types can be added within v1.
3. **Payloads grow at the tail only.** Parsers use `<` (minimum length), never `!=`,
   and supply defaults for absent tail fields. Precedents: HELLO accepts 13/14/N
   bytes; HELLO_ACK accepts 22 bytes, 24 bytes (`reason` stays `None`), and longer.
   Note the corollary at bytes 22-23: once a tail field ships, its *slot* is permanent
   even after the field itself dies — it becomes reserved, not reclaimed.
4. **Layout changes inside a message are signaled by a header flag**, not inferred
   from length — the video flags (`kVideoFlagIdr`, `kVideoFlagFrameEnd`, §4.8) show
   the pattern: per-datagram meaning rides in the header, never in guessed payload
   sizes. (The former precedent, `kSourceListFlagKind`, was removed 2026-07-27 with
   window sharing.)
5. **Network-supplied lengths and counts are never trusted**: every declared
   `count`/`len` is checked against the real payload size, clamped to receiver
   capacity, and oversized video/FEC payloads are rejected outright.

## 8. Source of truth

`core/include/deskhub/protocol/Wire.h` is the single implementation of this
specification, and its header comment designates this document as the textual source
of truth. Any change to a constant, layout, or rule in one **must** be made in the
other in the same change, with the round-trip tests in
`core/tests/protocol/WireTests.cpp` updated to match — two diverging copies of a wire
protocol are the hardest kind of bug to diagnose over UDP.
