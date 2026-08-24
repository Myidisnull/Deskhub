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
| Last reviewed `origin/main` tip | `8bdb40d3f44d4b2f66d89f22b414ada21c052a3d` |
| Reviewed tip short | `8bdb40d` |
| Reviewed tip date | 2026-08-21 05:29:21 +0000 |
| Reviewed tip subject | chore: fold new fuzz coverage into the seed corpus |
| Pin updated | 2026-08-21 |

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
