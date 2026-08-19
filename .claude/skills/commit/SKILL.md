---
name: commit
description: Write a Deskhub commit message that lands in the right release-notes section. Use when staging or committing changes in this repository, when asked to "commit", "write a commit message", or when a commit subject needs fixing before a tag is pushed. Enforces the conventional-commit types that scripts/changelog.sh turns into Added / Changed / Fixed / Removed / Security.
---

# Deskhub commit messages

Release notes are generated, not written: `scripts/changelog.sh` reads every commit
subject between two tags and sorts it into **Added / Changed / Fixed / Removed /
Security**. A subject is a line users read on the Releases page. Only the subject is read
— the body never reaches the changelog.

## Steps

1. **Look at what is actually being committed** — `git status --short` and
   `git diff --staged` (or `git diff` if nothing is staged yet). Never write a subject
   from the conversation alone.
2. **Split if needed.** One change per commit; two unrelated things can only land in one
   section. Say so and stage them separately.
3. **Pick the type** from the table below — this is the only thing that decides the
   section.
4. **Write the subject**: English, imperative, no trailing period, ~72 characters, about
   what changed for someone using Deskhub. Add a body only when the *why* is not obvious;
   the changelog ignores it.
5. **Run the repo checks that apply** — see *Before committing* below.
6. **Preview** when the commit is near a release: `scripts/changelog.sh` shows what the
   tag at HEAD would produce.

## Types

| Type | Section | For |
| --- | --- | --- |
| `feat:` | ✨ Added | something new a user can see or use |
| `fix:` | 🐛 Fixed | a bug that reached a user |
| `perf:` `refactor:` `style:` `revert:` | 🔧 Changed | same behaviour, reshaped |
| `security:` | 🔒 Security | hardening, a fixed vulnerability, a CVE bump |
| `docs:` `chore:` `ci:` `build:` `test:` `deps:` | omitted | internal, invisible to users |
| no type | 🔧 Changed | avoid — the section becomes an accident |

`type(scope)!: …` — scope is optional and free-form (`core`, `platform`, `linux`, `macos`,
`windows`, `android`, `ios`, `quic`, `terminal`, `ci`); `!` marks a breaking change and
prefixes the entry **Breaking**.

Two rules override the type:

- a subject starting with `remove`, `drop ` or `delete ` → **🗑️ Removed**
- a subject mentioning security, a vulnerability or a CVE → **🔒 Security**

## Before committing

- **Documentation ships in pairs.** Touching any `NAME.md` means updating `NAME.vi.md` in
  the same commit, and the other way round. English is authoritative.
- **`VERSION` must match the tag** that will be pushed — `scripts/check-version.sh`
  fails the deploy otherwise.
- **No comments in code** — that rule is enforced by review, not by CI; a diff that adds
  comments is not ready to commit.
- **`make test` and `make lint`** for anything touching `core/`, `platform/` or a client;
  `make lint-tidy` when C++ under `core/src` or `platform/src` changed. Docs-only commits
  need neither.

## Rewrites

| Instead of | Write |
| --- | --- |
| `Add Vietnamese documentation for installation and building instructions` | `docs: add Vietnamese install and build guides` |
| `Enhance security and authentication features` | `security: reject a second passcode guess on the same connection` |
| `Implement terminal sharing functionality` | `feat(terminal): share a shell from desktop hosts` |
| `Update CMake configurations and enhance QUIC build options` | `build: pin the quiche static runtime on MSVC` |
| `fix stuff` | `fix(android): keep the session alive when the screen locks` |

If a subject cannot be written without listing files, the commit is doing too much —
split it.
