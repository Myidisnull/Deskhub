# Requires GNU make. Runs on Windows, macOS and Ubuntu.
#
# This file is only the ENTRY POINT: it keeps the shared targets (bootstrap, clean)
# and includes the pieces under make/. One file per platform — adding a platform
# means adding make/<name>.mk plus one include line below, without touching the
# shared part.
#
#   make/toolchain.mk   HOST-dependent vars: SHELL, VsDevCmd, LLVM, NULDEV (include FIRST)
#   make/core.mk        shared core CMake tree: debug/release/test/test-ctest/coverage
#   make/windows.mk     Windows app — CMake (Win32 app, ONE Deskhub.exe)
#   make/macos.mk       macOS app   — xcodebuild
#   make/linux.mk       Ubuntu app  — CMake (GTK3 + native, ONE `deskhub`)
#   make/ios.mk         iOS app     — xcodebuild (Simulator)
#   make/android.mk     Android APK — Gradle (builds both the .so and the APK)
#   make/codestyle.mk   format/lint for C++ + Kotlin + Swift
#
# Windows uses cmd + VsDevCmd (it locates Visual Studio through vswhere, so it can be
# called from a plain cmd / PowerShell / Git Bash), macOS/Linux use sh + the system
# toolchain.
#   make bootstrap      install every dev dependency for the current OS (Android SDK, coverage too)
#   make                print the target list — NO platform is built implicitly, every
#                       platform must be named explicitly (see below)
#
# Explicit per-platform build/release/run — no platform is the default:
#   make build-windows   / release-windows   / run-windows   Win32 app, one Deskhub.exe (needs Windows + MSVC)
#   make build-macos     / release-macos     / run-macos     macOS app — both roles (needs macOS + Xcode)
#   make build-linux     / release-linux     / run-linux     Ubuntu app — both roles (needs Ubuntu + the -dev packages)
#   make build-android   / release-android   / run-android   debug APK / release APK (unsigned — see the notes)
#   make build-ios       / release-ios       / run-ios       iOS app for the Simulator (needs macOS + Xcode)
#
# run-windows takes ARGS="--share ...". run-android installs and opens on the connected
# device/emulator via adb; run-ios does the same on the Simulator.
#
# Distribution:
#   make dist-macos     macOS dmg signed with Developer ID + notarized + stapled
#   make verify-macos   check that Gatekeeper accepts the build that was just produced
#
# Shared CMake tree (core + platform + whatever client the current OS builds):
#   make debug          configure + build the debug preset
#   make release        configure + build the release preset
#
# Ubuntu, ONE-TIME permission grant for the host role (mouse/keyboard injection via /dev/uinput):
#   make setup-linux-permissions    udev rule + add the user to the `input` group
#
#   make test           build core_tests and run it (offline, no client/GPU needed)
#   make test-ctest     run through CTest (--output-on-failure) — matches how CI runs it
#   make coverage       measure core coverage (clang + llvm-cov — works on Windows/macOS/Ubuntu)
#
# Format/lint — all three languages, or one at a time:
#   make format         apply formatting in place for C++ + Kotlin + Swift
#   make lint           check style for all three without fixing (run before pushing to match CI)
#   make format-cpp     / lint-cpp      C++ only (clang-format: core/ platform/ client/)
#   make format-kotlin  / lint-kotlin   Kotlin only (ktlint: client/android)
#   make format-swift   / lint-swift    Swift only (swiftformat + swiftlint --strict:
#                                       client/apple + client/ios + client/macos)
#
#   make clean

# Must come BEFORE every include: GNU make's default goal is the FIRST target it sees,
# so putting this later would let `debug` from core.mk take the slot and turn a bare
# `make` back into an implicit build. A bare `make` prints the target list instead —
# no platform is privileged, each one is named explicitly.
all: help

include make/toolchain.mk
include make/core.mk
include make/windows.mk
include make/macos.mk
include make/linux.mk
include make/ios.mk
include make/android.mk
include make/codestyle.mk

help:
	@$(HELPCAT)

# Install every dev dependency (idempotent — already present means skipped).
bootstrap:
	@$(BOOTSTRAP)

clean:
	@$(RMRF) out

.PHONY: all help bootstrap clean
