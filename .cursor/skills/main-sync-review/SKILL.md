---
name: main-sync-review
description: >-
  Reviews and selectively ports new origin/main commits onto develop without
  merging branches, preserving customize settings/UI/layout. Use when the user
  asks to sync with main, compare main updates, classify Port/Skip/Adapt,
  refresh the main pin, or run the daily main-sync review.
---

# Main sync review (develop ← main)

`develop` and `main` are parallel. Never merge either direction. Selective port only.

## Authority (read first)

1. `.cursor/customize-main-base.md` — absorbed pin + last reviewed tip
2. `.cursor/customize-features.md` — customize features, settings, UI, hard skips
3. Rules: `develop-main-parallel`, `customize-from-main`, `main-sync-pin`, `customize-features`

## Modes

| Mode | Do | Update pin |
| --- | --- | --- |
| **Review only** (default daily) | Classify commits; no code port unless asked | Always advance **Last reviewed** |
| **Port** | Apply Port/Adapt items after classification | Reviewed always; **Last fully absorbed** only for commits taken whole |

Do not claim “synced with main” without updating the pin file.

## Workflow

Copy and track:

```
Main sync:
- [ ] Working tree clean (or intentional WIP only)
- [ ] git fetch origin
- [ ] Read customize-main-base.md + customize-features.md
- [ ] List: git log --oneline <Last reviewed>..<origin/main>
- [ ] Classify each commit (table below)
- [ ] Port/Adapt only if user asked (else review-only)
- [ ] Verify checklist
- [ ] Update customize-main-base.md
- [ ] If customize product surface changed: update customize-features.md
```

### 1. Range

```powershell
git fetch origin
git rev-parse origin/main
git log --oneline <Last-reviewed-short>..origin/main
```

If fetch fails, say so and classify against local `origin/main`; note stale risk.

Also keep awareness of `Last fully absorbed..origin/main` for unfinished Adapt items from older reviews.

### 2. Classify every commit

Output a table (newest or oldest first; be consistent):

| Commit | Subject | Class | Why | Touch zones |
| --- | --- | --- | --- | --- |

**Class**

| Class | When |
| --- | --- |
| **Port** | Shared infra/protocol/security/tests/build; no product surface change on develop |
| **Skip** | Conflicts with customize, restores deleted product (terminal/pairing/Devices), or merge-only noise |
| **Adapt** | Useful fix exists, but must be rewritten into develop shape (no main UX/settings/layout) |

Inspect with `git show --stat <commit>` and, when unclear, path-level diff. Merge commits → **Skip** (classify parents).

### 3. Hard Skip (unless user explicitly overrides)

- Terminal / PTY / Stop & Attach / `TerminalFfi` / TermGrid / terminal client UI
- Pairing / trust UI / Devices page / Devices nav
- main-only settings: `clientShell`, `clientDesktop`, `startHidden`, `allowNewPairings`
- Restoring `clientShell`-style flows or main branding over System Runtime
- Forcing main SPEC/ARCHITECTURE/product copy that reintroduces skipped surfaces

`keepAwake` is already on develop — not a main-only skip.

### 4. Prefer Port from these layers

`core/` (non-UI product), `platform/` (non-UI), `core/tests/`, CI/scripts that do not change release policy or app UX.

### 5. Product-surface gate → default Skip or Adapt

Paths/topics that usually must not land as-is from main:

- `UiSettings`, settings screens, Brand / i18n / encrypt / tray / background / log UI
- `client/*/` layout, navigation (keep 3-page Host·Client·Settings)
- Store metadata, icons, passcode/pairing narrative

If the underlying bugfix is valuable: extract into Adapt; keep develop UI.

### 6. Port execution (only when asked)

- Cherry-pick / patch / manual copy — never `git merge` / PR merge across branches
- Resolve conflicts toward customize-features
- After Adapt: no new terminal/pairing/Devices surface
- Run `make test`; if shared C++/scripts touched, `make lint`

### 7. Update `.cursor/customize-main-base.md`

Same change set as the review/port:

| Field | Rule |
| --- | --- |
| Last reviewed | Always set to the tip actually reviewed (`origin/main` or documented subset) |
| Last fully absorbed | Only advance to the highest `main` commit taken **in full** |
| Notes | Ported bullets + Skip/Adapt list (one line per commit or grouped) |
| Pin updated | Today’s date |

Partial ports: leave absorbed behind; document Adapt candidates for next pass.

### 8. Verify (required before calling done)

- [ ] Reviewed tip matches what was classified
- [ ] Absorbed tip not past unported commits
- [ ] No unintended UI / layout / settings / branding drift
- [ ] `make test` (+ `make lint` when required) for any port
- [ ] customize-features.md updated if customize inventory changed

## Output template (user-facing)

Reply in the user’s language. Keep it short:

1. Tip reviewed (`short` + subject) and fetch status
2. Classification table
3. Recommended next action (review-only done / Adapt list / Port plan)
4. Pin file status (updated or blocked)

## Do not

- Merge `main` ↔ `develop`
- Advance absorbed past unapplied commits
- Restore terminal / pairing / Devices from main by default
- Replace customize settings/UI/layout with stock main UX
- Port deploy/CI policy changes that drop test gates without an explicit ask
