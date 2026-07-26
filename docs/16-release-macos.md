# 16 — macOS Release (Developer ID + notarization)

How the macOS app reaches users, and why it takes a different route from every other
platform in this repo. Pairs with `14-macos-app.md` (what the app *is*) and
`13-release-mobile.md` (iOS/Android release automation).

## 1. Why not the Mac App Store

MAS requires `com.apple.security.app-sandbox`; it is validated automatically at upload,
before any human review. `client/macos/app/Deskhub.entitlements` sets it to `false`, so
the app cannot be submitted at all.

That entitlement is not a shortcut — the **agent (host) role** is built on two APIs the
sandbox forbids:

- `CGEventPost(kCGHIDEventTap, …)` (`agent/InputInjector.mm`) — injects mouse/keyboard
  into *other* apps. Requires Accessibility, and TCC does not grant Accessibility to
  sandboxed processes.
- `NSEvent addGlobalMonitorForEventsMatchingMask` (`agent/LocalInputMonitor.mm`) — the
  system-wide monitor behind the "host wins" rule. Same requirement, same refusal.

Neither has a workaround. Note the entitlements file also lists a fixed UDP port as a
reason; that part is **not** a real blocker — the sandbox allows binding and listening
with `network.server`, which the file already declares. ScreenCaptureKit and
`NSPasteboard` are likewise fine under sandbox. The host input path is the whole reason.

Since the sandbox is a per-process property and this is one app with both roles
(`01-architecture.md`), the host role rules out MAS for the entire binary. Same reason
AnyDesk, TeamViewer and BetterTouchTool all ship outside the store.

## 2. The two Mac deliverables

| | `client/macos` | `client/ios` on Mac |
|---|---|---|
| Roles | host + client | client only |
| Bundle id | `com.deskhub.macos` | `com.ios.deskhub` |
| Sandbox | off | on (inherited from iOS) |
| Channel | direct download (Developer ID + notarized) | Mac App Store |
| Build | `make dist-macos` | none — the *same* ipa as iPhone/iPad |
| Hardware | Intel + Apple Silicon | Apple Silicon only |

The iOS client reaches the Mac through **"Designed for iPad"**
(`SUPPORTS_MAC_DESIGNED_FOR_IPHONE_IPAD = YES` in `client/ios/Deskhub.xcodeproj`). It is
not a separate target, product, or CI job: the build that goes to TestFlight is the
build that runs on Apple Silicon Macs. After the first upload with the flag on, confirm
availability in App Store Connect → the app's availability section (Apple opts eligible
iOS apps in by default).

Its UI is still the finger-oriented virtual trackpad (`TouchInputView.swift`), so it is
strictly worse than the native app at being a Mac client — it exists for store presence,
not for quality. Mac users who want to *share* their Mac need the native app; the store
listing should say so.

The bundle ids differ deliberately: two different apps must not claim one id on one Mac.

## 3. The release flow

`make dist-macos` (see `make/macos.mk`) does all of it:

1. `make release-macos MACOS_SIGN=developerid` — xcodebuild with
   `CODE_SIGN_IDENTITY="Developer ID Application"`, `CODE_SIGN_STYLE=Manual`.
   Hardened Runtime is already `YES` in the project for both configurations, which
   notarization requires.
2. `ditto -c -k --keepParent` → `xcrun notarytool submit --wait` — Apple scans for
   malware. Automated, minutes, no functional review, no sandbox requirement.
3. `xcrun stapler staple` the `.app` — you submit a zip but can only staple an `.app` or
   `.dmg`; the zip is just transport.
4. `hdiutil create` a dmg holding the stapled app plus an `/Applications` symlink.
5. `codesign` + notarize + staple the **dmg** too, so the downloaded file itself
   validates offline.

`make verify-macos` then runs `stapler validate` and `spctl -a -vvv -t install`; expect
`accepted` and `source=Notarized Developer ID`.

No provisioning profile is involved. Unlike iOS, Developer ID only needs one when the app
uses restricted entitlements — this app declares only `network.client/server`.
`libcore.a` is a *static* library built by the project's CMake run-script phase, so it
links into the binary and needs no separate signature.

## 4. Why this matters more than the Gatekeeper dialog

macOS keys Screen Recording and Accessibility grants to the code signature. An ad-hoc
signature changes on every build, so users would re-grant permissions after **every
update** — and a missing Accessibility grant makes `CGEventPost` fail *silently*
(`agent/Permissions.h`), which reads as "the app is broken" rather than "a permission is
missing". A Developer ID signature is stable forever: grant once, done.

Switching from ad-hoc to Developer ID changes the signature one final time, so existing
testers re-grant once.

## 5. Certificate setup (one-time, local)

```
cd client/macos && fastlane mac certificates
```

Stores a `Developer ID Application` cert, encrypted, in the shared `Deskhub_certificate`
match repo — the same repo iOS uses, only `type: developer_id` instead of `appstore`
(`client/macos/fastlane/Matchfile`). Requires `ASC_KEY_*` and `MATCH_PASSWORD` in env.

Two ways this differs from the iOS certs:

- An account may hold only a limited number (5) of Developer ID Application certs, and
  revoking one affects everything already shipped with it. **Never** run `match nuke`
  against this type.
- Creating one requires the **Account Holder** role, not Admin.

## 6. CI wiring

- `.github/workflows/build.yml` — job `macos` builds Release with `MACOS_SIGN=adhoc` as a
  **build check** on every push/PR (no secrets, so it works on forks). Its artifact is
  deliberately *not* attached to any GitHub Release, for the signature-stability reason
  in §4.
- `.github/workflows/deploy.yml` — job `macos` (environment `stg`) runs
  `fastlane mac release` on `v*` tags, then attaches `deskhub-<tag>-macos.dmg` to the
  same GitHub Release that `build.yml` creates for the Windows zip (the signed apk is
  attached by the `android` job in the same deploy workflow).

**No new secrets.** `notarytool` takes the same App Store Connect API key already used
for TestFlight (`ASC_KEY_ID` / `ASC_ISSUER_ID` / `ASC_KEY_CONTENT`), and match uses the
same `MATCH_*` set. The fastlane lane is thin on purpose: it fetches the cert and writes
the `.p8` to a temp dir for `notarytool` (which reads a key *file*, unlike fastlane), then
shells out to `make dist-macos`.

## 7. Not done yet

- Nothing here has run on a Mac — see the verification note in `14-macos-app.md` §8.
  The first tag push is the first real execution of this path.
- No auto-update mechanism (Sparkle) and no Homebrew cask. Both become straightforward
  once notarized dmgs are published at a stable URL.
- `client/ios/fastlane/metadata/review_information/notes.txt` still tells App Store
  reviewers to download "a single client.exe" from Releases; the Windows artifact is now
  a zipped folder. Fix before the next iOS submission.
