# 15 — TODO after review of `core/` + `platform/`

Results of a full review of `core/` (7.7k lines) and `platform/` against industry standards, dated
**2026-07-26**. Each item records: **current state** (file:line), **why it is a problem**, **proposed
fix**, and **how to verify it is done**.

State at review time: `make test` **passes**, `core` includes no OS headers (the architectural
invariant is intact), style CI is green.

> **What to read first:** do §3 (build hygiene) FIRST. It is the cheapest and it is what will
> automatically catch most of the remaining bugs — especially ASan, whose absence is currently
> rendering the existing fuzz test useless.

---

## Summary table

> **Update 2026-07-26 (round 2).** A1, A3 and D3 have been fully implemented at the `core/` +
> `platform/` layer per the design in Claude Design ("Deskhub App" — the *Settings / password +
> trusted devices* screen). Details at the end of the file, under **Done**. What remains of A1 is
> the UI + keychain on the 4 platforms.

| # | Item | Group | Effort | Risk if left as-is |
|---|-----|------|------|----------------------|
| A1 | ~~No session authentication~~ → **core done**, UI/keychain remaining | Security | Medium | Anyone on the LAN can control the machine |
| A2 | Beacon is a UDP reflection amplifier | Security | Small | Host becomes a DDoS source against third parties |
| A3 | ~~Low-entropy `sessionId`~~ → **done** | Security | Small | Guessable → inject input/BYE |
| B1 | `kMaxNackIndices` is unreachable | Correctness | Very small | Silent count truncation (latent) |
| B2 | Unguarded unsigned time subtraction | Correctness | Small | Spurious disconnects (the LatencyTrace hang site was deleted with D1) |
| B3 | 100% FEC overhead on small frames | Performance | Small | Doubles packets exactly when the network is losing them |
| C1 | No warning flags outside MSVC, no sanitizers | Build | Small | The existing fuzz test catches nothing |
| C2 | No fuzzing of the parse layer | Build | Medium | The trust boundary is not truly enforced |
| C3 | Coverage has no threshold | Build | Very small | Coverage erodes and nobody notices |
| C4 | No `.clang-tidy` | Build | Small | Narrowing / bugprone issues slip through |
| D1 | ~~ClockSync + LatencyTrace are dead code~~ → **done: deleted** | Architecture | Medium | — |
| D2 | Discovery exists only on Windows | Architecture | Large | — (noted, not a bug) |
| D3 | `platform/Clock.h` leaks | Architecture | Small | **(1)(4) done**; (2)(3) remaining |

---

## 1. Security

### ✅ A1 — RESOLVED BY DECISION (2026-07-27): no authentication, by design

The owner decided the app targets **trusted LANs only** (or Tailscale, which is its own
trust boundary): the entire auth layer (GĐ10 password challenge–response, device tokens)
**was removed from core and every client**, together with LAN discovery
(DISCOVER/ANNOUNCE). The first HELLO goes straight through: Idle → issue `sessionId` →
Ready. Do not re-add per-client checks; if this decision is ever revisited, the gate
belongs in core before `state_.store(State::Ready)` (a protocol rule, same reasoning as
`SetInputAllowed`), and `git log` has the removed implementation to start from.

Consequence worth stating: anyone who can reach the host's UDP port can view and (if
allowed) control the machine. The port must never be exposed to untrusted networks.

---

### ⬜ A2 — `Beacon` is a UDP reflection amplifier

**Current state.** `core/src/discovery/Beacon.cpp` answers `LIST_SOURCES` — a **12-byte**
request, `sessionId = 0`, stateless, unauthenticated — with a `SOURCE_LIST` of up to **~569
bytes** (8 sources × (6 + 64 name bytes) + 1 + header; the per-record `kind` byte was removed
2026-07-27). Amplification factor **~47×** at the theoretical maximum — though in practice
names are now the short synthetic "Display N (WxH)" form (window titles left with window
sharing, 2026-07-27), so real replies are far smaller; the worst-case bound above is what the
wire format still permits.
(The DISCOVER → ANNOUNCE pair was removed 2026-07-27 with LAN discovery, which shrinks the
attack surface but the LIST_SOURCES amplification remains.)

There is no rate limit in core, **and none at the call site either**: `client/windows/cpp/
AgentLoop.cpp:924` calls `beacon.Reply` then `sendto` immediately, unconditionally.

**Why it is a problem.** UDP source addresses are trivially spoofed. An attacker broadcasts
`LIST_SOURCES` with the victim's IP as the spoofed source → every host in the range fires 577
bytes at the victim. On networks without egress filtering (BCP 38), the user's host becomes a
DDoS source against third parties.

The comment at the top of `Beacon.cpp:5-7` reasons about flood resistance, and that reasoning is
**correct but insufficient**: it only considers harm to *the host itself*, not a spoofed victim.
Update that comment as part of the fix.

**Fix.** A per-source-IP token bucket, placed **inside `Beacon`** (so all 4 platforms share it —
the very reason core exists) rather than in AgentLoop:

- `Beacon::Reply` takes an additional source-address parameter (as an opaque key — a `uint64_t`
  hash computed by the caller, so core need not know about `sockaddr`) and `nowUs`.
- A small fixed table (e.g. 16 entries, evicting the oldest) counts packets/second per key.
  Above the threshold (proposed: 5 pkt/s/source, 50 pkt/s total) → return 0.
- Clamp the `SOURCE_LIST` size: consider reducing `kMaxSourceNameBytes` in the broadcast reply,
  or return only `sourceCount` and make clients ask for details once they have a session.

**Verification.** Test case in the Beacon tests: 100 consecutive
`LIST_SOURCES` from the same key within 1 second → the number of non-zero `Reply` returns ≤ the
threshold; a different key is still answered normally within the same time window.

---

### ⬜ A3 — `sessionId` has low entropy and is partly attacker-chosen

**Current state.** `core/src/session/HostSession.cpp:53-55`:

```cpp
uint32_t sid = uint32_t(nowUs ^ (nowUs >> 32)) ^ m->clientId;
```

`m->clientId` comes from **the attacker's own HELLO**; `nowUs` is a monotonic counter with
predictable low bits. (A second occurrence lived in `HostRegistry` until LAN discovery was
removed on 2026-07-27.)

**Why it is a problem.** The comment at `HostSession.cpp:12-14` itself states that `sessionId`
is the *only fence* separating "my client" from the rest of the Internet. Guess it correctly and
you can inject `INPUT_EVENT`, `BYE`, `NACK` without being able to read the stream.

**Fix.** The cryptographic randomness source must live in `platform/` (core is forbidden from
touching the OS):

- Add `platform/include/deskhubp/Random.h` — `deskhubp::RandomU32()`: `BCryptGenRandom` on
  Windows, `getentropy`/`/dev/urandom` on POSIX.
- `HostSession` receives the sessionId via a callback or constructor instead of generating it
  itself, so core still knows nothing about the OS and tests can still inject deterministic
  values.

If fixed at the same time, fold it into a single commit with A1 (same code area, same theme).

---

## 2. Correctness & performance

### ⬜ B1 — `kMaxNackIndices = 593` can never be reached: the count is a u8

**Current state.** `core/include/deskhub/protocol/Wire.h:86` derives 593 from the MTU. `core/src/protocol/
Wire.cpp:248` accepts `indices.size() <= 593`, then `Wire.cpp:254` writes:

```cpp
p[4] = uint8_t(indices.size());   // 300 → 44
```

With 256–593 indices, the count byte is silently truncated and the receiver reads the wrong
count.

**Severity.** **Not live yet** — both call sites use `uint16_t nackIdx[64]`
(`client/windows/cpp/ClientApi.cpp:283`, `client/android/.../ClientLoop.cpp:563`). But
`HostSession.cpp:116` allocates `uint16_t idx[kMaxNackIndices]` = **1186 bytes of stack** for a
ceiling that can never exceed 255.

**Fix (pick one).**

- Simple: `kMaxNackIndices = 255`, and fix the comment to explain that the real ceiling is the
  width of the count field, not the MTU. Also reclaims 1 KB of stack in HostSession.
- Or: widen the count to u16 and update `04-protocol.md`. Not worth it — clients only request a
  few missing fragments of the frame at the head of the queue; 255 is more than enough.

**Verification.** `core/tests/protocol/WireTests.cpp` — `BuildNack` with 256 indices must return 0.

---

### ⬜ B2 — Unsigned time subtraction: one module defends, the others do not

**Current state.** The (now-removed) `HostRegistry` guarded explicitly, with a comment stating
outright that `nowUs` **can go backward** between two loop iterations (a monotonic clock can
still read differently across cores):

```cpp
if (nowUs > hosts_[i].lastSeenUs && nowUs - hosts_[i].lastSeenUs > staleUs_)
```

The same expression is **unguarded** at:

| Location | Consequence when nowUs goes backward |
|-----|------------------------|
| ~~`LatencyTrace.cpp:28`~~ | ~~**Hang.**~~ Deleted 2026-07-27 together with the module (D1) |
| `core/src/session/ClientSession.cpp:181` | Spurious disconnect: `Die("lost contact with host (timeout)")` |
| `core/src/session/HostSession.cpp:156` | Spurious disconnect: `Disconnect()` |
| `core/src/transport/Reassembler.cpp:207` | Spurious frame drop + IDR request (IDR is expensive — exactly when it isn't needed) |
| `core/src/transport/Reassembler.cpp:66` | `maxGapMs_` statistic becomes garbage |

**Why it is a problem.** The removed `HostRegistry` comment was right — and nothing else in
core applies its guard, so every location above still trusts a clock that can step backward.

**Fix.** Settle on "nowUs CAN go backward" (safer, and Clock.h promises nothing to the contrary),
then add a shared helper in `core/include/deskhub/protocol/` or a new utility header:

```cpp
// Safe time delta: if nowUs is behind the mark, return 0 instead of wrapping to a huge number.
inline constexpr uint64_t ElapsedUs(uint64_t nowUs, uint64_t sinceUs) {
    return nowUs > sinceUs ? nowUs - sinceUs : 0;
}
```

Replace at the 4 remaining locations above (so there is only one way to write it).

**Verification.** One case per module: call `Tick`/`PopReady` with a `nowUs` smaller than
the previous call → no state change, no spurious disconnect/drop.

---

### ⬜ B3 — FEC overhead is 100% on small frames, not 1/8 as documented

**Current state.** `core/src/transport/Packetizer.cpp:38`: `numGroups = ceil(count / 8)`.
Actual overhead = `numGroups / count`:

| count (packets in frame) | numGroups | Overhead |
|---|---|---|
| 1 | 1 | **100%** |
| 2 | 1 | 50% |
| 4 | 1 | 25% |
| 8 | 1 | 12.5% |
| 14 (P-frame ~16 KB) | 2 | 14% |

`core/include/deskhub/protocol/Wire.h:62` claims "the bandwidth cost remains exactly =
1/kFecGroupSize" — which is only asymptotically true.

**Why it is a problem.** P-frames on a static screen are routinely 1–2 packets. FEC is enabled by
`BitrateController` **exactly when the link is already losing packets**
(`BitrateController.cpp:32`, threshold ≥1%), meaning we double the packet count of small frames
precisely when the network is congested.

**Fix.** Skip parity when `count < kFecGroupSize` — the parity of a 1-element group **is a
duplicate of that packet**, which `Wire.h:311` itself already admits. Proposed threshold: only
emit FEC when `count >= kFecGroupSize` (i.e. overhead ≤ 12.5% as designed). Small frames that
lose packets are already covered by NACK (`RetransmitCache`) — far cheaper because it only costs
when loss actually happens.

After the fix, update the "bandwidth cost" passage in `Wire.h:53-63` and
`06-transport.md`.

**Verification.** `core/tests/transport/FecTests.cpp`: a 1-packet frame with
`SetFecEnabled(true)` → `SendFrame` emits exactly 1 datagram, with no parity packet.

---

## 3. Build hygiene & verification — **DO THIS GROUP FIRST**

### ⬜ C1 — No warning flags outside MSVC, no `-Werror`, no sanitizers anywhere

**Current state.** `core/CMakeLists.txt:38`:

```cmake
if(MSVC)
    target_compile_options(core PRIVATE /W4 /permissive- /sdl)
endif()
# ← no else()
```

Core is compiled by **GCC** (CI Ubuntu), **AppleClang** (CI macOS), **NDK clang** (Android),
**Xcode** (iOS) — all at the **default warning level**, i.e. nearly mute.

Grepped `sanitiz|asan|ubsan|fsanitize|Werror|Wall|Wextra` across `Makefile`, `make/`, every
`CMakeLists.txt`, every workflow in `.github/`: **not a single hit**.

**Why this is the most important item in the file.** It is currently neutralizing the best test
the project ever wrote for itself: `core/tests/protocol/WireTests.cpp:516` — *"300 garbage buffers
through every Parse*"*. Without ASan, that test only catches hard crashes; **the heap overreads
it was written to find pass through silently**. Writing a fuzz test and running it without
sanitizers is paying for goods and never picking them up.

**Fix.**

1. `core/CMakeLists.txt` — add a non-MSVC branch:
   ```cmake
   else()
       target_compile_options(core PRIVATE -Wall -Wextra -Wconversion -Wshadow)
   endif()
   ```
   Do the same for the `core_tests` target. Expect: `-Wconversion` will produce a batch of
   warnings at the intentional narrowing sites (`LinkStats.cpp:88-91`, `Reassembler.cpp:270`) —
   add explicit `static_cast`s; that is exactly the point of the flag.

2. `CMakePresets.json` — add an `asan` preset:
   ```json
   {
     "name": "asan",
     "inherits": "x64-debug",
     "cacheVariables": {
       "CMAKE_CXX_COMPILER": "clang++",
       "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer",
       "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined"
     }
   }
   ```

3. `make/core.mk` — an `asan` target that runs `core_tests` under the sanitizer, following the
   same pattern as the existing `coverage` target.

4. `.github/workflows/build.yml` — add one job (Ubuntu, cheap) that runs `make asan`. Group it
   with `core-tests` so its pass/fail signal is independent of the artifact builds.

5. Enable `-Werror` **in CI only**, not in local builds (to avoid blocking devs on a new warning
   from a newer compiler).

---

### ⬜ C2 — No fuzzing of the parse layer

**Current state.** `core/src/protocol/Wire.cpp` is the trust boundary of the entire program — its own
header says so at lines 22-23. The existing test (`WireTests.cpp:516`) uses a fixed-seed
xorshift32: the right instinct, but still only ~300 static vectors.

**Fix.** A ~30-line libFuzzer target is enough and is the industry standard for network parsers:

```cpp
// core/fuzz/WireFuzz.cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n) {
    std::span<const uint8_t> pkt(d, n);
    const auto h = deskhub::ParseCommonHeader(pkt);
    if (!h) return 0;
    const auto p = deskhub::PayloadOf(pkt);
    // dispatch on h->type through ALL Parse* functions — exactly like HostSession's switch
    ...
}
```

Build on top of the `asan` preset (§C1), run 60 seconds per CI run with the corpus committed to
the repo. Coverage priorities: `ParseSourceList`, `ParseInputEvents`, `ParseNack` —
all of them read a count field declared by the peer.

Phase 2 (not urgent): fuzz the **state machine** too, not just the parser — feed random datagram
sequences into `HostSession::HandlePacket` + `Tick`.

---

### ⬜ C3 — Coverage is measured but has no threshold

`.github/workflows/build.yml` runs `make coverage` and uploads the HTML as an artifact, but
nothing fails when coverage drops (a comment in the workflow already acknowledges this). Add a
floor threshold for `core/src` — read from `llvm-cov report`, compared against a number committed
to the repo. Set the threshold at the current level as the baseline, only allowed to move up.

---

### ⬜ C4 — No `.clang-tidy`

The style tooling is genuinely above average: clang-format / ktlint / swiftformat / swiftlint are
all **version-pinned** and CI checks them with **the exact same script devs run locally** — no
changes needed there.

What is missing is static analysis for the things a formatter cannot see. Add a `.clang-tidy` at
the root with `bugprone-*`, `cert-*`, `cppcoreguidelines-narrowing-conversions`,
`performance-*`. Run it on `core/` first (small, cleanest), expand to `client/` later.
`CMAKE_EXPORT_COMPILE_COMMANDS: ON` already exists in the preset, so no extra infrastructure is
needed.

---

## 4. Architecture & dead code

### ✅ D1 — RESOLVED (2026-07-27): the unused e2e latency system was deleted (option b)

`control/ClockSync.{h,cpp}`, `control/LatencyTrace.{h,cpp}`, the e2e half of `LinkStats`
(`AddE2e` + the `e2eMsAvg`/`e2eMsMax`/`e2eSamples` fields), and their test cases in
`ControlTests.cpp` were removed. No client ever called any of it — every ClientLoop ships
its own inline estimate (`e2e = now − (ackDelta − minRTT/2) − frame pts`) and that is now
the only implementation, documented in `06-transport.md` §8 and `09-diagnostics.md`.
Side effect: the B2 hang site at `LatencyTrace.cpp:28` no longer exists.

---

### ✅ D2 — RESOLVED (2026-07-27): LAN discovery removed entirely

The DISCOVER/ANNOUNCE feature and `HostRegistry` were removed project-wide; `Beacon`
(now `core/session/Beacon`) only answers pre-session LIST_SOURCES + PING.

---

### ⬜ D3 — `platform/Clock.h` leaks

Four problems in one 111-line file:

1. **Unguarded macros.** `platform/include/deskhubp/Clock.h:69-70`:
   ```cpp
   #define WIN32_LEAN_AND_MEAN
   #define NOMINMAX
   ```
   Any TU that already defines them (very common — most files in `client/windows/cpp` do) gets
   **C4005 macro redefinition**. Core builds at `/W4`; adding `/WX` (§C1) breaks the build.
   Standard form: `#ifndef X` / `#define X` / `#endif`.

2. **`NowUs()` in the global namespace.** The include path is `deskhubp/Clock.h`, CMake names
   the library `platform`, yet the function is not in `namespace deskhubp`. A public header
   exporting a global named `NowUs` is a name collision waiting to happen. Move it into
   `namespace deskhubp` and fix the call sites (grep `NowUs()` in `client/`).

3. **`<windows.h>` into every consumer.** The file's own comment (`Clock.h:62-63`) criticizes
   pulling in all of `windows.h`, then does exactly that to every TU that includes it. Convert
   `platform` to **STATIC** + `Clock.cpp` — `platform/CMakeLists.txt:6-8` already anticipates
   this step. The price is one non-inlined call per network loop iteration: negligible next to a
   `sendto`.

4. **`QueryPerformanceFrequency` return value unchecked** (`Clock.h:77`). On failure → `freq`
   uninitialized → division by garbage or by zero. A single `if` fixes it.

---

## Proposed implementation order

1. **§3 C1** — warning flags + ASan preset + CI job. Cheapest, and it automatically catches the
   rest.
2. **§4 D3** — `Clock.h`. Small, and its item (1) is a prerequisite for enabling `/WX` in the
   step above.
3. **§2 B1, B2, B3** — three independent bugs, one commit + one test case each.
4. **§1 A2, A3** — security items requiring no design decision, doable immediately.
5. **§4 D1** — decide (a) or (b), then do it.
6. **§1 A1** — decide (a) or (b), then do it. Touches the UI on all 4 platforms, so leave it for
   last.
7. **§3 C2, C3, C4** — ramp up gradually once the items above are green.

---

## Done (2026-07-26, round 2)

Implemented per the Claude Design *"Deskhub App"* — screens `06 · settings / password +
trusted devices` (desktop) and `05 · settings / password` (mobile). The design settled on a
**challenge-response** mechanism, not password transmission: *"kept in the keychain — never sent.
the client answers a challenge, the password stays on this Mac."*

**New:**

| File | Role |
|------|---------|
| `core/include/deskhub/crypto/Sha256.h` + `src/crypto/Sha256.cpp` | SHA-256, HMAC, PBKDF2, constant-time compare — pure C++20 |
| `core/include/deskhub/auth/PasswordAuth.h` + `src/auth/PasswordAuth.cpp` | Handshake math, stateless |
| `core/include/deskhub/auth/AuthGuard.h` + `src/auth/AuthGuard.cpp` | Password, 3-strikes/5-minute lockout, trusted devices |
| `platform/include/deskhubp/Random.h` | CSPRNG: BCrypt / arc4random / getrandom |
| `core/tests/crypto/CryptoTests.cpp` | Reference vectors from FIPS 180-4, RFC 4231, RFC 7914 |
| `core/tests/auth/AuthTests.cpp` | 12 case groups, most asserting that something is **rejected** |

**Modified:** `Wire.{h,cpp}` (AUTH_CHALLENGE 0x09 / AUTH_RESPONSE 0x0A, `RejectReason`,
tail-appended fields for HELLO/HELLO_ACK), `HostSession.{h,cpp}` (`Authenticating` state,
`InSession`, `BeginSession`, `GrantInput`), `ClientSession.{h,cpp}` (`SetPassword`,
`AnswerChallenge`, per-reason rejection messages), `AgentLoop.cpp` ×2 (wired `randomBytes`),
`Clock.h` (macro guards + QPF check), `docs/04-protocol.md` §5.4.

**Why implement SHA-256 ourselves instead of linking a library:** each platform has a different
library (BCrypt / CommonCrypto / not available in the Android NDK) → four `#ifdef` branches in
the middle of the core, exactly what `core/` exists to avoid. SHA-256 is a fixed algorithm, ~150
lines, verifiable against published vectors. Entropy, however, **cannot** be self-implemented —
it is the one thing that lives in `platform/`.

**Notable decision — fail closed.** If `HostCallbacks::randomBytes` is not wired, the host
**rejects all connections** rather than falling back to a clock-derived `sessionId`. Falling back
would rebuild the exact A3 weakness just removed, on a rarely-exercised path nobody would ever
test. Consequence: every test that constructs a `HostSession` must wire `TestRandomBytes` (done),
and every real host must wire `deskhubp::RandomBytes` (wired on Windows + macOS).

### Remainder of A1 — UI + keychain

*(Historical — superseded 2026-07-27: the entire auth layer was removed, see A1 above. None of
the items below will be done; the WinUI3 frontend they referenced was also deleted the same
day. Kept as a record of the round-2/3 work.)*

The core part is done and tested. **iOS + Android (client role) are done** — see the dedicated
section below. Remaining:

- ⬜ **Windows (host role)** — Settings screen (WinUI3) per `DesktopSettings`: require checkbox,
  password field + confirm + Generate, the `0/3 · 5 min` counter pair, trusted-devices list
  + Forget. Store `AuthKey` + the list in DPAPI/Credential Manager via
  `onTrustedDevicesChanged`.
- ⬜ **macOS (host role)** — same screen, stored via Keychain Services.
- ⬜ **Windows/macOS (client role)** — password prompt when `onPasswordNeeded` fires; store the
  token via `onDeviceToken`. Templates already exist in `client/ios` and `client/android`, port
  directly.
- ⬜ **Input approval dialog** — `SetAskBeforeInput(true)` already holds the session in view-only
  mode; still missing the prompt itself and the button that calls `GrantInput()`.

### iOS + Android — ✅ done (2026-07-26, round 3)

Both are **client-only** by design (`phoneIsClient` / `hostingDesktopOnly`), so the work was the
client end of the handshake. The C++ facade is identical on both platforms:
`ClientLoop::Credentials` (clientId / deviceName / password / deviceToken),
`Phase::NeedPassword`, `SubmitPassword`, `TakeDeviceToken`, `rejectReason`.

| | iOS | Android |
|---|---|---|
| Secret store | `Credentials.swift` — Keychain, `kSecAttrAccessibleWhenUnlockedThisDeviceOnly` | `Credentials.kt` — Android Keystore + AES/GCM |
| Bridge | `dh_start`(+4 params), `dh_submit_password`, `dh_take_device_token`, `dh_reject_reason` | `nativeStart`(+4), `nativeSubmitPassword`, `nativeTakeDeviceToken`, `nativeRejectReason` |
| Password prompt | `StreamView.passwordOverlay` | `StreamActivity.PasswordOverlay` |
| Saved passwords | `ConnectView.savedPasswordSection` | `MainActivity` (below Recents) |
| Biometrics | Face ID via `LocalAuthentication` ✅ | ⬜ not yet (see below) |

**Bug caught while wiring:** `hello.clientId = uint32_t(NowUs())` — the clientId changed on every
run, while `AuthGuard` keys the trusted-devices list **by clientId**. The token would never match
again and the user would have to type the password forever. It is now a random value generated
once and stored in the Keychain/Keystore; falls back to the clock if the caller forgets to pass
it (loses the remember-device feature, still connects).

**Why Android does not use `androidx.security:security-crypto`:** `EncryptedSharedPreferences`
was deprecated by Google (2024). Adding an unmaintained dependency merely moves the debt
elsewhere; Keystore + AES/GCM is a platform API and is leaner than the configuration that library
demands.

**Not done:** biometrics on Android (`BiometricPrompt` requires the extra `androidx.biometric`
dependency + a host Activity). iOS already has Face ID because `LocalAuthentication` costs only
~25 lines and no new dependency.

⚠ **iOS has not been compile-verified** — the dev machine is Windows, Xcode cannot run here.
Android (C++/JNI/Kotlin) and core build green locally; iOS has only passed
`swiftformat --lint` (0/21 files needing fixes). The `ios` job of `.github/workflows/build.yml`
(`make build-ios` on macos-latest) is where Swift/ObjC++ compile errors will first be caught.

### Deliberately not done

- **Stream encryption** remains Phase 6 — authentication and encryption are two different things,
  and `04-protocol.md` §5.1 says so in the most visible place.
- **`AuthGuard` holds one in-flight challenge** (v1: one client per session). This also prevents
  opening thousands of parallel challenges to grind down host memory.
- **PBKDF2 runs on the client's network thread** (~100 ms during handshake, before video). The
  derived key is cached by `(salt, iterations)` so replayed HELLOs do not recompute.

## Related

- `01-architecture.md` §Security — where the encryption deferral (DTLS/AEAD) is recorded
- `04-protocol.md` — wire spec, must be updated together with A1/B1/B3
- `05-roadmap.md` Phase 6 — where an **authorization** item must be added (currently only
  encryption)
- `06-transport.md` — describes FEC, must be updated with B3
- `09-diagnostics.md` — describes the e2e system, must be updated with D1
