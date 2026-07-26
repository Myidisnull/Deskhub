fastlane documentation
----

# Installation

Make sure you have the latest version of the Xcode command line tools installed:

```sh
xcode-select --install
```

For _fastlane_ installation instructions, see [Installing _fastlane_](https://docs.fastlane.tools/#installing-fastlane)

# Available Actions

## Mac

### mac certificates

```sh
[bundle exec] fastlane mac certificates
```

Tạo/cập nhật cert Developer ID trong repo match (chạy tay, cần vai Account Holder)

### mac import_cert

```sh
[bundle exec] fastlane mac import_cert
```

Import cert Developer ID (tạo tay trên developer.apple.com) vào repo match

### mac release

```sh
[bundle exec] fastlane mac release
```

Build Release ký Developer ID + notarize + staple + dmg

----

This README.md is auto-generated and will be re-generated every time [_fastlane_](https://fastlane.tools) is run.

More information about _fastlane_ can be found on [fastlane.tools](https://fastlane.tools).

The documentation of _fastlane_ can be found on [docs.fastlane.tools](https://docs.fastlane.tools).
