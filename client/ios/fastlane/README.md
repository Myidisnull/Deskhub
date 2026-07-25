fastlane documentation
----

# Installation

Make sure you have the latest version of the Xcode command line tools installed:

```sh
xcode-select --install
```

For _fastlane_ installation instructions, see [Installing _fastlane_](https://docs.fastlane.tools/#installing-fastlane)

# Available Actions

## iOS

### ios certificates

```sh
[bundle exec] fastlane ios certificates
```

Tạo/cập nhật cert + profile trong repo match (chạy tay khi setup lần đầu hoặc cert hết hạn)

### ios release

```sh
[bundle exec] fastlane ios release
```

Build ipa Release (ký bằng match) và upload TestFlight

### ios metadata

```sh
[bundle exec] fastlane ios metadata
```

Đẩy store listing (fastlane/metadata) lên App Store Connect

----

This README.md is auto-generated and will be re-generated every time [_fastlane_](https://fastlane.tools) is run.

More information about _fastlane_ can be found on [fastlane.tools](https://fastlane.tools).

The documentation of _fastlane_ can be found on [docs.fastlane.tools](https://docs.fastlane.tools).
