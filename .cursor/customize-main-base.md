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
| Last reviewed `origin/main` tip | `737d0b269ad93e70bfb9286df9489de6b0547091` |
| Reviewed tip short | `737d0b2` |
| Reviewed tip date | 2026-08-17 14:17:12 +0700 |
| Reviewed tip subject | feat: Refactor snapshot contains checks to ensure reading order in terminal tests |
| Pin updated | 2026-08-17 |

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
