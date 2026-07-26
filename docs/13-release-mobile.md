# 13 — Mobile Release & CI

How the Android and iOS clients are built, signed, and shipped to Google Play
(internal/closed testing) and TestFlight. Everything below is derived from the
current configuration: `client/android/fastlane/`, `client/ios/fastlane/`,
`.github/workflows/`, the root `VERSION` file, `make/android.mk`,
`make/ios.mk`, and `client/android/app/build.gradle.kts`. For the apps
themselves see 08-android-client.md and 12-ios-client.md.

## Release architecture overview

fastlane is the single source of truth for app identity and version numbers.
CI (GitHub Actions) only provides secrets and a build number; the lanes do the
actual work, so every lane can also be run locally with the same env vars.

| Platform | Lane | What it does | Runs where |
|---|---|---|---|
| Android | `android release` | Builds a signed release AAB (`gradle bundleRelease`), uploads to the Play **internal** track; optionally promotes the same versionCode to a closed track (`PLAY_PROMOTE_TRACK`, e.g. `alpha`) | CI (`deploy.yml`) or locally |
| Android | `android metadata` | Pushes the Play store listing (supply) from `client/android/fastlane/metadata/android/` | CI (`metadata.yml`) or locally |
| iOS | `ios release` | Builds a signed release ipa (`build_app`, manual signing via match), uploads to **TestFlight** | CI (`deploy.yml`) or locally |
| iOS | `ios metadata` | Pushes the App Store listing (deliver) from `client/ios/fastlane/metadata/` | CI (`metadata.yml`) or locally |
| iOS | `ios certificates` | Creates/renews the Apple Distribution cert + provisioning profile in the match repo (`match(type: "appstore", readonly: false)`) | **Local only** — CI uses match read-only |
| macOS | `mac release` | Fetches the Developer ID cert via match, then runs `make dist-macos` (build → notarize → staple → dmg). No store upload — the app cannot go on the Mac App Store; see `16-release-macos.md` | CI (`deploy.yml`) or locally |
| macOS | `mac certificates` | Creates/renews the **Developer ID Application** cert in the same match repo (`match(type: "developer_id", readonly: false)`) | **Local only** — needs the Account Holder role |

App identity:

- Android package: `com.manhpham.deskhub` (`client/android/fastlane/Appfile`).
  Independent from the Gradle `namespace` (`com.deskhub.app`), which is kept
  for JNI symbol names.
- iOS bundle id: `com.ios.deskhub`, team `UPJRMYQ38F`
  (`client/ios/fastlane/Appfile`). The same build also ships to Apple Silicon
  Macs as "Designed for iPad" — no separate target, product or CI job; see
  `16-release-macos.md` §2.
- macOS bundle id: `com.deskhub.macos`, same team
  (`client/macos/fastlane/Appfile`) — deliberately **different** from iOS, since
  the native macOS app is a different app (host role, sandbox off) that would
  otherwise collide with the iOS-on-Mac build on one machine.

Automated by CI: build, sign, upload to internal/TestFlight, artifact upload,
GitHub Release creation. Manual/local only: initial cert creation
(`fastlane ios certificates`), the very first manual AAB upload to Play
Console, screenshots, and store-console compliance forms.

## GitHub Actions workflows

### build.yml

Triggers: push to `main`, push of `v*` tags, pull requests to `main`,
`workflow_dispatch`. Uses **no secrets** — the Android job here builds a
*debug* APK (debug-signed, installable as-is).

| Job | Runner | What it does |
|---|---|---|
| `core-tests` | ubuntu + macos matrix | `make test-ctest` — offline C++20 core tests |
| `coverage` | ubuntu | `make coverage` (clang + llvm-cov), uploads `coverage-report` artifact |
| `windows` | windows (needs core-tests) | CMake preset `x64-release` (native DLL) + CTest, then `dotnet build -c Release -p:Platform=x64` (WinUI3); uploads `deskhub-windows` — the whole unpackaged self-contained folder, not a single exe; checks out submodules |
| `android` | ubuntu | JDK 17, NDK `26.1.10909125`, CMake `3.22.1`, then `make build-android`; uploads `deskhub-android` (app-debug.apk) |
| `ios` | macos | `make build-ios` — Simulator build check only, no signing, no artifact |
| `macos` | macos (needs core-tests) | `make release-macos MACOS_SIGN=adhoc`, uploads `deskhub-macos` (zip). **Build check only** — the ad-hoc artifact is deliberately never attached to a Release; the shippable dmg comes from `deploy.yml`. See `16-release-macos.md` §4 |
| `release` | ubuntu (needs windows, android) | Only on `v*` tags: zips the Windows folder, renames the apk with the tag, and attaches both to a GitHub Release (`softprops/action-gh-release`). The macOS dmg is appended to the same Release by `deploy.yml` |

### deploy.yml

Triggers: push of `v*` tags, or `workflow_dispatch` with input
`promote_track` (choice: `none` (default) or `alpha`). All jobs declare
`environment: stg` — secrets live in that GitHub environment, not in
repository secrets. `BUILD_NUMBER` is set to `github.run_number` in every job.

| Job | Runner | Steps |
|---|---|---|
| `ios` | macos-latest | Checkout → `fastlane ios release` (working dir `client/ios`) → upload `deskhub-ios-ipa` artifact |
| `macos` | macos-latest | Checkout → `fastlane mac release` (working dir `client/macos`; match fetches the Developer ID cert, then `make dist-macos` builds → notarizes → staples → packages the dmg) → `make verify-macos` → upload `deskhub-macos-dmg` artifact → on tags, attach `deskhub-<tag>-macos.dmg` to the GitHub Release. Reuses the iOS `ASC_*` and `MATCH_*` secrets — no new ones. See `16-release-macos.md` |
| `android` | ubuntu-latest | Checkout → JDK 17 → Android SDK → `sdkmanager "ndk;26.1.10909125" "cmake;3.22.1"` → Ruby 3.3 + `gem install fastlane` → decode `ANDROID_KEYSTORE_BASE64` to `$RUNNER_TEMP/release.keystore` → `fastlane android release` → upload `deskhub-android-aab` artifact |

On a tag push the `inputs` context is empty, so `PLAY_PROMOTE_TRACK` is empty
and only the internal track is updated (live within minutes, no full review).
Promotion to the closed `alpha` track — which is reviewed per build — happens
only when a maintainer dispatches the workflow and selects `alpha`. Promotion
*adds* the versionCode to the target track; it does not remove it from
internal.

### metadata.yml

Trigger: `workflow_dispatch` only. Pushes store listings without rebuilding
the apps. Both jobs use `environment: stg`. The `ios` job (macos-latest) runs
`fastlane ios metadata` with the `ASC_*` secrets; the `android` job
(ubuntu-latest, Ruby 3.3 + fastlane) runs `fastlane android metadata` with
`PLAY_JSON_KEY`.

### lint.yml

Triggers: push to `main`, pull requests to `main`, `workflow_dispatch`. No
secrets. Jobs: `codestyle` (pinned clang-format 22.1.3 + ktlint 1.5.0 +
swiftformat 0.62.1 via `scripts/codestyle.sh --check` — the same script as
`make lint`), `swiftlint` (`swiftlint lint --strict` on
`client/ios/app/swift` and `client/macos/app/swift`), and `android-lint`
(`./gradlew lint`, uploads `android-lint-report`).

### Secrets (GitHub environment `stg`)

| Secret | Used by | Must contain |
|---|---|---|
| `ASC_KEY_ID` | deploy.yml (ios), metadata.yml (ios) | App Store Connect API Key ID |
| `ASC_ISSUER_ID` | deploy.yml (ios), metadata.yml (ios) | Issuer ID from the same ASC Keys page |
| `ASC_KEY_CONTENT` | deploy.yml (ios), metadata.yml (ios) | The `.p8` key file, base64-encoded as one line (`base64 -i AuthKey_XXX.p8`) |
| `MATCH_GIT_URL` | deploy.yml (ios) | HTTPS remote of the certificate repo, e.g. `https://github.com/manhpham90vn/Deskhub_certificate.git` |
| `MATCH_PASSWORD` | deploy.yml (ios) | Passphrase that encrypts certs/profiles in the match repo |
| `MATCH_GIT_BASIC_AUTHORIZATION` | deploy.yml (ios) | base64 of `<user>:<PAT>` where the PAT can read the certificate repo (`echo -n "user:token" \| base64`) |
| `ANDROID_KEYSTORE_BASE64` | deploy.yml (android) | Release keystore, base64-encoded (`base64 -i release.keystore`) |
| `KEYSTORE_PASSWORD` | deploy.yml (android) | Keystore store password |
| `KEY_ALIAS` | deploy.yml (android) | Key alias inside the keystore |
| `KEY_PASSWORD` | deploy.yml (android) | Password of that key |
| `PLAY_JSON_KEY` | deploy.yml (android), metadata.yml (android) | Verbatim JSON (not base64) of a Google service account with Release + Store presence permissions on `com.manhpham.deskhub` |

## Versioning

- **Display version** (`x.y.z`): the `VERSION` file at the repo root
  (currently `0.1.1`), shared by both platforms. Both Fastfiles read it
  directly (`File.read(.../VERSION).strip`). Note: only fastlane consumes
  `VERSION` — the Makefile, `make/*.mk`, and Gradle do not read it.
- **Build number**: env `BUILD_NUMBER`, set by CI to `github.run_number`
  (monotonically increasing per workflow run).
  - Android: becomes `versionCode` (fallback `1` if unset); `VERSION` becomes
    `versionName`. Fastlane injects both plus `applicationId` into Gradle via
    `-P` properties, overriding the fallbacks in
    `client/android/app/build.gradle.kts` (`versionCode 1`,
    `versionName "0.1-dev"` — used only for manual builds).
  - iOS: becomes `CURRENT_PROJECT_VERSION` (local fallback: timestamp
    `%Y%m%d%H%M`); `VERSION` becomes `MARKETING_VERSION`. Injected via
    `xcargs`, overriding the values in `client/ios/Deskhub.xcodeproj`, which
    remain only as fallbacks for plain Xcode builds.

Because `versionCode`/build number come from the run number of `deploy.yml`,
re-running the workflow always yields a fresh, higher build number.

## Signing

### Android

`client/android/app/build.gradle.kts` creates a `release` signing config
**only when the env var `KEYSTORE_FILE` is set**, reading `KEYSTORE_FILE`,
`KEYSTORE_PASSWORD`, `KEY_ALIAS`, and `KEY_PASSWORD` directly from the
environment. Without these, release builds are unsigned (Play rejects them);
debug builds always use the default debug key. In CI, `deploy.yml` decodes
`ANDROID_KEYSTORE_BASE64` into `$RUNNER_TEMP/release.keystore` (outside the
workspace) and passes its path as `KEYSTORE_FILE`. `make release-android`
(`make/android.mk`, `gradlew assembleRelease`) therefore produces an unsigned
APK unless the same four env vars are exported.

### iOS

Signing uses **fastlane match** (`client/ios/fastlane/Matchfile`): certs and
provisioning profiles are stored encrypted in a separate private git repo
(`storage_mode "git"`, branch `main`, type `appstore`; default `git_url` is
the SSH remote `git@github.com:manhpham90vn/Deskhub_certificate.git`,
overridable via `MATCH_GIT_URL` — CI sets the HTTPS URL and authenticates
with `MATCH_GIT_BASIC_AUTHORIZATION`). The `release` lane calls `setup_ci` on
CI (temporary keychain), then `match` in **read-only** mode — CI never
creates or revokes certificates. The build is signed manually
(`CODE_SIGN_STYLE=Manual`, identity `Apple Distribution`, profile
`match AppStore com.ios.deskhub`). Authentication with Apple uses an App
Store Connect API key (`ASC_*` env vars), never an Apple ID password. When
the cert expires, run `fastlane ios certificates` locally.

## Store metadata

- **Android** (`supply`): `client/android/fastlane/metadata/android/<locale>/`
  with locales `en-US` and `vi`; each holds `title.txt`,
  `short_description.txt`, `full_description.txt`,
  `changelogs/default.txt`, and `images/featureGraphic.png`. The `metadata`
  lane uploads text + images (screenshots and changelogs are skipped; the
  changelog is attached by the `release` lane when it uploads the AAB). The
  lane anchors its edit to the `internal` track, so it only works once at
  least one release exists there.
- **iOS** (`deliver`): `client/ios/fastlane/metadata/<locale>/` (`en-US`,
  `vi`) with `name`, `subtitle`, `description`, `keywords`,
  `promotional_text`, `release_notes`, `marketing_url`, `privacy_url`,
  `support_url`; plus root-level `copyright.txt`, `primary_category.txt`,
  `secondary_category.txt` and a `review_information/` directory (reviewer
  contact + demo credentials). The lane uses `skip_binary_upload`,
  `skip_screenshots`, and `force: true` (no interactive HTML preview
  confirmation on CI).

Editing a listing is a text change: edit the files, commit, then run the
**metadata** workflow (Actions → metadata → Run workflow) — no app rebuild.

## One-time setup checklist

1. **Store apps**: register `com.ios.deskhub` on App Store Connect (team
   `UPJRMYQ38F`) and create the app `com.manhpham.deskhub` on Play Console.
2. **ASC API key**: create a team API key in App Store Connect (Users and
   Access → Integrations → API Keys); record Key ID, Issuer ID, and the `.p8`
   file → secrets `ASC_KEY_ID`, `ASC_ISSUER_ID`, `ASC_KEY_CONTENT` (base64).
3. **match certificate repo**: create the private repo
   `Deskhub_certificate`; run `cd client/ios && fastlane ios certificates`
   locally with `ASC_*` and a chosen `MATCH_PASSWORD` exported — this
   generates and pushes the encrypted cert + `match AppStore com.ios.deskhub`
   profile. Create a PAT with read access to that repo → secrets
   `MATCH_GIT_URL`, `MATCH_PASSWORD`, `MATCH_GIT_BASIC_AUTHORIZATION`.
4. **Android keystore**: generate a release/upload keystore (`keytool
   -genkeypair ...`), keep it safe → secrets `ANDROID_KEYSTORE_BASE64`,
   `KEYSTORE_PASSWORD`, `KEY_ALIAS`, `KEY_PASSWORD`.
5. **Play service account**: create a Google Cloud service account with a
   JSON key, enable the Google Play Android Developer API, and invite the
   account in Play Console with Release (testing tracks) and Store presence
   permissions on the app → secret `PLAY_JSON_KEY` (verbatim JSON).
6. **First AAB manually**: Play Console requires the first release to be
   uploaded through the web UI (Internal testing → Create release) before the
   API works; both the `release` upload and the `metadata` lane fail until an
   internal release exists ("Could not find release for version code ''").
7. **GitHub environment**: create environment `stg` (Settings →
   Environments) and add all eleven secrets from the table above as
   *environment* secrets — `deploy.yml` and `metadata.yml` jobs read them via
   `environment: stg`.
8. **Testers**: configure internal testers on the Play internal track (and
   the closed `alpha` track if promotion is used) and TestFlight testers in
   App Store Connect.
9. **Console compliance** (manual, both stores): screenshots (the pipeline
   skips them), App Privacy / Data safety declarations, content rating, ads
   declaration, target audience.

## Cutting a release end to end

1. Bump the root `VERSION` file (e.g. `0.1.1` → `0.2.0`).
2. Update release notes:
   `client/ios/fastlane/metadata/{en-US,vi}/release_notes.txt` and
   `client/android/fastlane/metadata/android/{en-US,vi}/changelogs/default.txt`.
3. Commit, then tag and push:

   ```sh
   git tag v0.2.0
   git push origin v0.2.0
   ```

   The tag triggers **deploy.yml** (iOS → TestFlight, Android → Play
   internal, ipa/aab artifacts) and **build.yml** (desktop + debug-apk
   artifacts gathered into a GitHub Release).
4. Alternatively, run Actions → **deploy** → *Run workflow*; choose
   `promote_track: alpha` to additionally promote the Android build from
   internal to the closed `alpha` track (per-build review applies). Tag
   pushes never promote.
5. Wider rollout (closed → production, TestFlight → App Store review) is done
   manually in the store consoles.
6. Local equivalent (fastlane installed, env vars from the secrets table
   exported): `cd client/ios && fastlane ios release` /
   `cd client/android && fastlane android release`.
