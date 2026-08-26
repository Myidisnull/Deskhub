# Customize / main base pin

English governs this file. Update it every time `develop` successfully absorbs `origin/main`.

| Field | Value |
| --- | --- |
| Customize branch | `develop` |
| Upstream branch | `main` (`origin/main`) |
| Last fully absorbed `main` commit | `085f7ef47485589b17ece152ee8cae58bafecd2a` |
| Short | `085f7ef` |
| Date | 2026-08-16 23:35:55 +0700 |
| Subject | feat: Add TODO for implementing Stop & Attach shell functionality across clients |
| Last reviewed `origin/main` tip | `00b54f3b2d036b2fb1475199b74c8df4515a8690` |
| Reviewed tip short | `00b54f3b` |
| Reviewed tip date | 2026-08-25 10:48:57 +0800 |
| Reviewed tip subject | Merge branch 'manhpham90vn:main' into main |
| Pin updated | 2026-08-26 |

## Partial Adapt (2026-08-26) — P0–P3 + deskhub-cli mandatory

User asked to finish remaining P0–P3 and make `client/cli` mandatory (UDP Adapt, no HostLink/QUIC).

| Theme | What landed | Notes |
| --- | --- | --- |
| **deskhub-cli** | `client/cli` + `core/cli` + `make build-cli` / `cli-smoke`; Win links `deskhub_win_view` + `AgentLoop`; Linux splits `deskhub_linux_core`; `FileTransferClient` UDP headless send; X11 viewer uses `ClientEngine` | Connect/share/shell/send on develop UDP |
| **P1 Files connect-once** | Win/Linux AskConnectSurface includes Files; standalone send via CLI/`FileTransferClient` | Desktop app + CLI |
| **P2 Stop & Attach / Android pairing** | Host rows attach + Android Settings pairing (prior pass) | develop-shaped |
| **P3 VERSION** | Android `versionName` ← `VERSION`; Apple `MARKETING_VERSION` = 5.2.0; brand stays System Runtime | |
| **P3 fuzz** | `core/fuzz/seeds` refreshed from `origin/main` | |
| **P3 perf** | Port `core/perf` + `DESKHUB_CORE_PERF`; `test.yml` gates release builds with `core_perf` only | **Skipped** quiche/`platform_perf` |

`Last fully absorbed` stays `085f7ef` (selective Adapt). `Last reviewed` stays `00b54f3b` (fetch failed this pass; local tip unchanged).

## Partial Adapt (2026-08-25) — user-requested: Shell / pairing / HostLink-intent / VERSION 5.x / fuzz

User explicitly asked to add the former Skip themes. Policy in `.cursor/customize-features.md` §4.

| Theme | What landed this pass | Still queued |
| --- | --- | --- |
| **VERSION 5.x** | `VERSION` → **5.2.0**; brand remains System Runtime / `.system-runtime` | Store listings / README version strings if any lag |
| **fuzz** | `core/fuzz/seeds` + regressions from main (~6065 seeds); `fuzz_term` target | — |
| **Shell / 终端** | `core/terminal/*` + tests; `Chan::Terminal` + `TermSize` helpers (no TermOpen MsgType — develop Noise IDs keep 0x50+) | PTY host, TerminalSession UDP, client UI, connect-once Shell |
| **配对 UX** | `TrustStore` + `PairedDevices` core + tests | Devices/Trust UI; wire beside SessionCrypto |
| **HostLink** | **not** imported; multi-surface stays UDP connect-once | Shell surface completes the trio |
| Also | Port `1c5bd26a`: integration crash handlers + pid `NextTestPort` + Windows dump upload in `test.yml` | — |

`Last fully absorbed` stays `085f7ef` (selective Adapt, not contiguous).

## Review only (2026-08-25) — full eval through `00b54f3b`

`git fetch` OK. Range since prior reviewed tip `e7f12085`: **1** non-merge (+ 2 merges).

| Commit | Subject | Class | Why |
| --- | --- | --- | --- |
| `1c5bd26a` | feat(tests): enhance crash reporting and add process ID handling | **Port** | Integration crash handlers (abort/purecall/invalid-param) + pid-hashed `NextTestPort` + Windows LocalDumps upload in `test.yml`; no product/customize surface |
| `39d0a5fb` / `00b54f3b` | merge tips | **Skip** | Merge-only |

Full gap `085f7ef..00b54f3b` ≈ **160** non-merge. Feature-status summary (develop UDP vs main HostLink/QUIC):

| Capability | develop | Verdict |
| --- | --- | --- |
| Audio / Opus / Settings | Adapted | done |
| File xfer UDP + acceptFiles + viewer send UI | Adapted | done |
| LinkPulse + viewer health/disconnect | Adapted | done |
| Reattach grace (60s / stable clientId) | Adapted | done |
| Connect-once Desktop/Files (no Shell) | Partial Adapt (iOS/macOS/Android) | Win/Linux UI still direct-open |
| Phone files-only host | Partial Adapt (iOS/Android + `dha_start_files`) | OK while app open |
| PcmRing / UdpSocketWin / PDB / CodeQL arch / iOS panel split | Port/Adapt | done |
| `1c5bd26a` crash dumps + pid ports | not yet | **next Port** |
| Terminal / Shell / pairing / Devices / CLI / HostLink / VERSION 5.x | — | Skip (or huge Adapt if asked) |
| Fuzz corpus folds | — | Skip / low Port |
| Perf gates / platform_perf QUIC | — | Adapt only if develop wants CI perf (no quiche) |

`Last fully absorbed` stays `085f7ef`. Recommended next: Port `1c5bd26a`, then Win/Linux connect-once UI if desired.

## Partial port (2026-08-25) — P1/P2 connect-once Desktop/Files + phone file receive

Adapted from main connect-once / phone files / CodeQL arch (UDP shape; **no** Shell / HostLink / pairing):

- **P1** `cd32b07c`: CodeQL macOS `ARCHS=arm64 ONLY_ACTIVE_ARCH=YES`
- **P1** `d328874c`: iOS `StreamControlPanel` in `StreamOverlays.swift`
- **P1** `390f13ba` FileHost thread safety already present on develop
- **P2** Connect-once → Connected pick Desktop / Send files (no Shell): iOS + macOS + Android; open-files auto file picker after stream up
- **P2** Phone UDP file receive: `dha_start_files` / `HostEngine` files-only; iOS `FilesHost` while app open + `acceptFiles` + not broadcasting; Android same while MainActivity alive; sender offer timeout 60s in `FileUpload`
- Strings/FFI: `DHStrOpenDesktopLabel` / `OpenFilesLabel` / `ConnectedPickSession`; `DHHostCaps` on `dh_list_sources`

Still deferred: Windows/Linux connect-once UI polish; standalone file-send without a desktop session; Shell branch of connect-once.

`Last fully absorbed` stays `085f7ef`.

## Review only (2026-08-25) — full feature eval through `e7f12085`

`git fetch` OK. Range since prior reviewed tip `55ba8a2`: **2** non-merge commits.

| Commit | Subject | Class | Why |
| --- | --- | --- | --- |
| `d328874c` | chore(ios): split stream control panel out of StreamView | **Adapt** (done) | See P1/P2 partial port above |
| `ff9d0202` | chore: fold new fuzz coverage into the seed corpus | **Skip** / low **Port** | Corpus noise unless develop actively fuzzes |

Full gap `085f7ef..e7f12085` (~159 non-merge): shared infra themes already partially Adapted (audio, FrameGate/NVENC/mailbox, AutoShareGate, CI apt/workflows, PcmRing, LinkPulse, file xfer UDP, reattach grace). Remaining value is thin; hard skips (terminal, HostLink/QUIC connect-once Shell, CLI, pairing/Devices, VERSION 5.x) still dominate.

`Last fully absorbed` stays `085f7ef`.

## Partial port (2026-08-25) — viewer link health UI + disconnect (0db0a9a7)

Adapted from main `0db0a9a7` (develop UDP / ClientSession shape; no ScreenFfi / Reattaching phase):

- FFI: `dh_link_ping_text`, `DHStrDisconnectButton`, `DHStrLinkReattaching` (maps to `kReconnecting`)
- macOS: `ConnectionStatusBar` + health polling in `StreamModel`
- iOS / Android: control panel link row, disconnect label, reattaching banner (`wasStreaming && connecting`)
- Linux: header bar link label + disconnect button (link moved out of window title)

Windows: link health already in title; close window disconnects (unchanged).

## Partial port (2026-08-25) — viewer reattach grace (stable clientId)

Adapted from main `5b53ab43` (develop `ClientEngine` shape; no HostLink / Reattaching phase / ScreenViewer):

- `ClientReconnect`: 60s grace (`kClientReconnectGraceUs`) + `ClientReconnectStillWorthTrying`
- `ClientEngine` NetThread: one `clientId` for the whole reconnect loop so the host rebinds via `FindByClient`; time-based grace instead of 5-attempt cap
- Status still uses existing `kReconnecting` / Connecting phase (no new FFI phase)

Skipped from that commit: `ScreenSessionEnd`, `ClientPhase::Reattaching`, HostLink recover/redial.

## Partial port (2026-08-25) — file transfer polish (Stop notifies sender + viewer leave)

Adapted from main `dad4c5f3` / peer-gone wiring (develop UDP shape):

- `FileHost::Stop`: flush `NotAccepting` cancel outbox to senders before `LinkLost` / clear
- `HostEngine::AttachSession`: `onViewerLeave` → `FileHost::OnPeerGone` (was DrainGone-only)
- Core test: host stops taking mid-batch → sender reason `NotAccepting`

`cfe609f6` (DrainGone / DiskKeepingUp half backlog) already present on develop.

## Partial port (2026-08-25) — client file send UI + acceptFiles setting

- Settings: `UiSettings.acceptFiles` / `accept_files=`; host `AgentOptions.acceptFiles` via ShareFlow / Win+Linux share start / AgentSession FFI
- Settings UI: Win / Linux / macOS / iOS / Android toggle “Accept files…”
- Viewer send: Win system menu + Ctrl+O; Linux Ctrl+O; macOS toolbar + fileImporter; iOS control panel; Android control panel (copy URI → cache → `dh_session_file_send`)
- Strings / locale / FFI IDs `DHStrAcceptFilesLabel` / `DHStrSendFilesLabel`

## Partial port (2026-08-25) — client file send (session UDP)

Adapted from main client upload path (develop shape; no HostLink / QUIC / TransferView):

- `ClientNetLoop`: route `Chan::File` before video pump; `onFile` hook
- `ClientEngine`: `FileUpload` + `RecordStream` on the existing viewer UDP socket; `BeginFileSend` / `CancelFileSend` / progress API
- FFI: `dh_session_file_send` / `dh_session_file_cancel` / `dh_session_file_busy` / `dh_session_file_progress` / `dh_session_file_error`

## Partial port (2026-08-25) — file transfer platform (host receive + upload)

Adapted from main `FileHost` / `FileStore` / `FileUpload` (develop UDP shape; no QUIC/HostLink/TransferView):

- `core`: `RecordStream` for record reassembly
- `platform/net/FileUdp.h`: UDP record tunnel for messages larger than one datagram (`FileRecord` 0x76)
- `platform`: `FileStore`, `FileHost`, `FileUpload`; `AgentOptions.acceptFiles`; `HostEngine` + `HostNetLoop` file channel routing
- Default save dir: app data `files/` or `~/.system-runtime/files`

## Partial port (2026-08-25) — file transfer core protocol

Adapted from `6833fd65` `54bbe4f7` `390f13ba` (core only; no platform `FileHost` / client UI / CLI):

- `core/protocol`: `Chan::File`, `FileOffer`…`FileCancel` wire messages; record framing (`BuildRecord`/`ReadRecord`); `kHostAcceptsFiles` cap bit
- `core/session`: `FileSender`, `FileReceiver`, `FileTransfer` shared types (no `TrustStore` dependency)
- `core/transfer`: `Crc32`, `SafeName`
- Tests: `WireTests` record/file round-trip, `FileTransferTests`, `Crc32Tests`, `SafeNameTests`

## Partial port (2026-08-24) — LinkPulse / ClientEngine / PDB / link health UI

Adapted from `9d8cce72` `a5dc5cf8` `0db0a9a7` `9dd664e4` (develop shape; no HostLink / pairing UI):

- `core`: `LinkPulse` + tests; integrated into `ClientSession` (replaces naive ping RTT; stall → reconnect path)
- `core`: link quality strings + i18n catalog rows
- `platform`: `ClientEngine` restart join fix; `LinkHealth()` + FFI `dh_session_link_health` / `dh_link_quality_text`
- Root `CMakeLists.txt`: MSVC PDB flags
- Viewers: Win/Linux title + Apple `StreamModel` + Android control panel show quality/ping (existing status line unchanged)

Prior partial port (2026-08-24): PcmRing / UdpSocket / integration stacks / deps — see below.

## Partial port (2026-08-24) — PcmRing / UdpSocket / integration stacks / deps

Ported from `2fe3c45c` `390f13ba` `bbe0dffd` `6bdb9075` `7b13af4a` `cc14f072` (infra only; no UI/settings/product surface):

- `core`: `PcmRing` header + `PcmRingTests`
- `platform`: audio sinks (Wasapi / Pipewire+`PwStream` / AAudio / CoreAudio) use shared `PcmRing`
- `platform`: `UdpSocketWin` singleton `WSAStartup` (no per-socket `WSACleanup`)
- `tests/integration`: Windows symbolized crash + `std::terminate` stacks (`dbghelp`)
- CI: CodeQL actions 4.37.7; Android compose-bom 2026.08.00 + Gradle 9.7.1

Skipped entire ranges (customize product / main-only surfaces): file transfer + `HostLink` refactor + connect-once desktop/shell/files UI, mobile file receive, link pulse/reattach viewer UI, `deskhub-cli`, terminal/shell, pairing/trust UX, v5.2 docs/VERSION/store metadata, perf/CLI CI gates, fuzz seed folds.

`Last fully absorbed` stays at `085f7ef`. Reviewed tip `55ba8a2`.

## Partial port (2026-08-21) — CI apt-mirror / workflow split / release-notes

Adapted from `addabc29` `ccd070e2` `0c5ee462` `8e039215` `6d4fd075` `396d4a01` `015d4eba` `417147f0`
(skipped `c8ce95d` dropping tests gate; skipped quiche/opus/CLI steps from main’s desktop/mobile jobs;
skipped main pairing/QUIC release-note narrative):

- `scripts/apt-install.sh` (mirror rotation + timeouts) wired into Linux apt steps
- `scripts/changelog.sh` + commit links for GitHub Release notes
- Workflows: `ci.yml`, `build-desktop.yml`, `build-mobile.yml`, `test.yml`, `nightly.yml`
  (replaced `build.yml` / `apps.yml` / `tests.yml` / `fuzz-nightly.yml`)
- `deploy.yml`: still `build-desktop` **needs: test**; System Runtime Windows artifact path kept
- README badge URLs → `ci.yml` / `nightly.yml` only

`Last fully absorbed` stays at `085f7ef`. Reviewed tip remains `8bdb40d`.

## Partial port (2026-08-21) — NVENC + RgbDownscale + Apple PTS

Ported / adapted (skipped ARCHITECTURE docs; kept System Runtime CMake skip messages):

- `core`: `RgbDownscale` + tests; `VideoPacer` + tests
- Linux: `HwEncoder` (VA-API dma-buf / NVENC mapped), `NvEncoder`, capture-thread area downscale into mailbox, `DESKHUB_NVENC` CMake option
- Apple: `VtDecoder` PTS pacing via control timebase + congestion fallback to display-immediately

`Last fully absorbed` stays at `085f7ef`. Reviewed tip remains `8bdb40d`.

## Partial port (2026-08-21) — Linux encode mailbox (`505e06b8` + `447f4849`)

Ported / adapted (no NVENC, no RgbDownscale, no ARCHITECTURE docs):

- `core`: header-only `FrameMailbox` + tests
- `core`: `AgentDiagCaps.queueDrop` / `q_drop=` in `evt=sum` + diag test
- Linux: mapped frames copy → encode thread via mailbox; dma-buf stays inline; `q_drop` on displaced frames
- `CopiedFrame` / `FrameFromCopy` in `CaptureTypes.h`

(Superseded for NVENC/downscale/PTS by the section above.)

`Last fully absorbed` stays at `085f7ef`. Reviewed tip remains `8bdb40d`.

## Partial port (2026-08-21) — `1860da47` FrameGate

Ported (core only; skipped ARCHITECTURE.md / `.vi.md`):

- `FrameGate` running due-time cadence so non-multiple capture rates (e.g. 40→30) meet target instead of collapsing to ~20 fps
- Matching `FrameGateTests` (awkward rate / slow capture / idle no burst)

`Last fully absorbed` stays at `085f7ef` (selective port, not a contiguous absorb). Reviewed tip remains `8bdb40d`.

## Review only (2026-08-21) — through `8bdb40d` (local `origin/main`; `git fetch` failed)

Range `c8ce95d..8bdb40d`: **75** commits (~64 non-merge). Grouped classification:

| Theme | Class | Representative commits | Notes |
| --- | --- | --- | --- |
| Terminal scrollback / repaint / grid / Quic+TerminalHost errors | **Skip** | `17178159` `45de7287` `ecf3b6b0` `77b9abdb` | Hard skip — no terminal product on develop |
| CLI client (`deskhub-cli`) + smoke/loader | **Skip** (default) | `a99dd379` `dfdb3eb6` `f3cb8139` `83be350a` `5e096f27` | New product surface; only Adapt if customize wants a CLI |
| Audio streaming (wire + jitter + Opus + all OS share/hear) | **Adapt** (in progress → develop shape) | `105164c3`…`1b88a817` + follow-ups | See **Adapt port — audio** below |
| Video smooth pipeline (Linux encode mailbox/NVENC/downscale; Apple PTS pacing; FrameGate FPS) | **Ported** | `1860da47` FrameGate; `505e06b8`+`447f4849` mailbox; `51d6530c`+`44e88d97`+`6f4b2bef` NVENC/RgbDownscale; `793bfb3c` Apple PTS | Video smooth batch absorbed into develop shape |
| CI apt-mirror / workflow split / release-notes | **Ported** (Adapt) | `addabc29`…`8e039215` + split/changelog; **kept** `needs: test` | No quiche/opus/CLI; System Runtime path kept |
| Fuzz seed corpus folds | **Skip** or low-pri **Port** | `8ba2dff0` `c5b0f000` `47ee0600` `8bdb40d` | Noise unless fuzzing actively used on develop |
| Docs/README/store metadata/icons/VERSION 5.0.x | **Skip** | `114ab727` `921eb0d5` `e77b98ac` `41ed0395` `b479f82c` (mostly) | Customize owns product copy / System Runtime / 4.0.2; badge URL-only update done with CI Port |
| Store screenshots tooling (iOS/Android) | **Adapt** optional | `0de6c2d9` `3940f2af` | Scripts OK if rebranded; skip Deskhub store assets |
| macOS build arch args / bootstrap toolchain | **Adapt** | `cd32b07c` + bits of `b479f82c` scripts | Take script fixes without icon/README churn |

**Recommended next:** commit audio Adapt batch, macOS arch/bootstrap Adapt, or fuzz seeds.

## Adapt port (2026-08-21) — audio streaming (complete B)

Develop-shaped Adapt (no merge; no terminal / pairing / Devices / CLI / quiche):

- Wire: `AudioPacket`, `HostCaps.audio`; **`kClientWantsAudio = 1<<1`** (encrypt stays bit 0)
- Core: `AudioJitterBuffer` + tests; `UiSettings.shareAudio` / `playAudio`; ShareFlow
- Platform: Opus (`DeskhubOpus.cmake`, soft-off stub if missing), sinks, broadcaster/player;
  `HostEngine` / `ClientEngine` / `dha_offer_audio`; FFI share/play + string IDs 127/128
- Clients: Win/Linux capture; macOS SCStream audio; iOS ReplayKit + resampler; Android
  `AudioShare` + RECORD_AUDIO; all five Settings UIs + LocaleCatalog
- CI: opus cache/build in `build-desktop` / `build-mobile` / `test` / `codeql`;
  `scripts/bootstrap.sh` builds host opus; THIRD_PARTY_NOTICES (+ `.vi.md`)

`Last fully absorbed` stays at `085f7ef` (no contiguous whole-commit absorb).

## Adapt port (2026-08-21) — AutoShareGate from `7229892` / `963493a`

Ported into `develop` shape (no merge; no terminal / pairing / Devices):

- `core`: `AutoShareGate` + tests; `kWaitingForDisplays` / `kNoDisplayFound` + locale catalog rows
- `platform`: `dh_auto_share_probe_ms` / `dh_auto_share_step` + string IDs
- Desktop auto-share waits for displays before launching share: Windows (`MainFrame`), Linux GTK (Gdk monitor probe), macOS (`waitForShareSources` / `autoShare`)
- Automatic share failures soft-fail to host status (no modal) via `ReportShareProblem` / wait note

Still skipped from that commit range: `LinkRecovery`, Terminal\* / TerminalViewer / Quic keepalive, terminal docs, deploy.yml dropping `tests` gate.

`Last fully absorbed` stays at `085f7ef` (no whole `main` commit taken).

## Review note — earlier tip `c8ce95d` (superseded)

Classification of `737d0b2..c8ce95d` (AutoShareGate Adapt applied):

| Commit | Class | Notes |
| --- | --- | --- |
| `7229892` Implement automatic sharing and terminal link recovery | **Adapt** (done) / **Skip** (rest) | AutoShareGate path ported; terminal/LinkRecovery/Quic keepalive skipped |
| `963493a` style: Format getter methods in AutoShareGate | **Adapt** (bundled) | Taken with AutoShareGate |
| `9384c4e` Merge PR #9 fix/terminal | **Skip** | Merge commit |
| `c8ce95d` Remove tests job dependency from deploy | **Skip** | keep `apps needs: tests` on develop |

Prior skip list from 2026-08-17 still applies for `085f7ef..737d0b2`.

Next sync: `git fetch origin` then re-diff `8bdb40d..origin/main`.

## Partial port (2026-08-17) — reviewed through `737d0b2`

Ported into `develop` shape (no merge; UI/layout/settings unchanged):

- Host capabilities on `SOURCE_LIST` flags (`HostCaps` / Wire / Beacon / SourceQuery / ConnectDriver / HostEngine / ClientFfi optional `out_caps`)
- Input takeover: release held keys/buttons while suppressed (`InputApplier`)
- Matching core + integration tests (`FakeAgent.allowInput`)

Skipped (conflict with customize or deleted on `develop`):

- Terminal Stop & Attach, `TermGridFill`, `TerminalFfi`, terminal client UI (all platforms)
- Pairing / Connect / HostPage / AgentModel UX changes from `main`
- QUIC `build-quiche.sh`, Makefile/make module churn, `apps.yml` icon/asset changes
- Fuzz seed corpus restore, `todo.md`, product SPEC/ARCHITECTURE copy for terminal
- `dh_host_has_terminal` / attach-shell string IDs (no terminal product surface)

`Last fully absorbed` stays at `085f7ef` because no complete `main` commit was taken whole. Next sync: re-diff `085f7ef..origin/main` and skip items already listed above.

## How to refresh the pin

Do **not** merge `main` into `develop`. After a selective port from `origin/main` completes cleanly (and verify checklist in `.cursor/rules/main-sync-pin.mdc` passes):

```powershell
git rev-parse origin/main
git log -1 --format="%H%n%ci%n%s" origin/main
```

Advance **Last fully absorbed** only when a `main` commit (or contiguous range) is taken in full. Always update **Last reviewed** when a review/partial port reaches a new tip, and document skipped items.
