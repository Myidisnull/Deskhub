# Customize / main base pin

English governs this file. Update it every time `develop` successfully absorbs `origin/main`.

| Field | Value |
| --- | --- |
| Customize branch | `develop` |
| Upstream branch | `main` (`origin/main`) |
| Last absorbed `main` commit | `085f7ef47485589b17ece152ee8cae58bafecd2a` |
| Short | `085f7ef` |
| Date | 2026-08-16 23:35:55 +0700 |
| Subject | feat: Add TODO for implementing Stop & Attach shell functionality across clients |
| Pin updated | 2026-08-17 |

## How to refresh the pin

After `git merge origin/main` (or equivalent) completes cleanly:

```powershell
git rev-parse origin/main
git log -1 --format="%H%n%ci%n%s" origin/main
```

Write those three lines into the table above and set **Pin updated** to today.
