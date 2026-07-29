# Cần GNU make. Chạy được trên Windows, macOS và Ubuntu.
#
# File này chỉ là ĐIỂM VÀO: giữ target dùng chung (bootstrap, clean) rồi include các
# mảnh trong make/. Mỗi nền tảng một file, thêm nền tảng mới = thêm make/<tên>.mk và
# include thêm một dòng ở dưới — không phải đụng vào phần chung.
#
#   make/toolchain.mk   biến theo HOST: SHELL, VsDevCmd, LLVM, NULDEV (include ĐẦU TIÊN)
#   make/core.mk        cây CMake lõi dùng chung: debug/release/test/test-ctest/coverage
#   make/windows.mk     app Windows  — CMake (app Win32, MỘT Deskhub.exe)
#   make/macos.mk       app macOS    — xcodebuild
#   make/linux.mk       app Ubuntu   — CMake (GTK3 + native, MỘT `deskhub`)
#   make/ios.mk         app iOS      — xcodebuild (Simulator)
#   make/android.mk     APK Android  — Gradle (tự dựng cả .so lẫn APK)
#   make/codestyle.mk   format/lint C++ + Kotlin + Swift
#
# Windows dùng cmd + VsDevCmd (tự tìm Visual Studio qua vswhere nên gọi được từ
# cmd / PowerShell / Git Bash thường), macOS/Linux dùng sh + toolchain hệ thống.
#   make bootstrap      cài toàn bộ dependency dev cho OS hiện tại (cả Android SDK, coverage)
#   make                build debug cây desktop OS hiện tại (Windows: client + core; Unix: core)
#   make release        build release cây desktop OS hiện tại
#
# Build/release RÕ theo từng nền tảng:
#   make build-windows   / release-windows   app Windows native (Win32, một Deskhub.exe)
#   make build-macos     / release-macos     app macOS — cả hai vai (cần macOS + Xcode)
#   make build-linux     / release-linux     app Ubuntu — cả hai vai (cần Ubuntu + gói -dev)
#   make build-android   / release-android   APK debug / APK release (chưa ký — xem ghi chú)
#   make build-ios       / release-ios       app iOS cho Simulator (cần macOS + Xcode)
#
# Phát hành:
#   make dist-macos     dmg macOS ký Developer ID + notarize + staple (docs/16-release-macos.md)
#   make verify-macos   kiểm tra Gatekeeper chấp nhận bản vừa dựng
#
#   make run            build + chạy Deskhub.exe (app Win32, mới có Windows), ARGS="--share ..."
#   make run-macos      build + mở app macOS (cần macOS + Xcode)
#   make run-linux      build + chạy app Ubuntu (cần Ubuntu)
#
# Ubuntu, cấp quyền MỘT LẦN cho vai host (bơm chuột/bàn phím qua /dev/uinput):
#   make setup-linux-permissions    quy tắc udev + thêm user vào nhóm `input`
#   make run-android    build + cài + mở app Android trên máy/emulator đang kết nối (adb)
#   make run-ios        build + cài + mở app iOS trên Simulator (cần macOS + Xcode)
#   make test           build core_tests rồi chạy (offline, không cần client/GPU)
#   make test-ctest     chạy qua CTest (--output-on-failure) — khớp cách CI chạy
#   make coverage       đo phủ core (clang + llvm-cov — chạy trên cả Windows/macOS/Ubuntu)
#
# Format/lint — cả 3 ngôn ngữ hoặc rõ từng ngôn ngữ:
#   make format         áp format tại chỗ cho cả C++ + Kotlin + Swift
#   make lint           kiểm tra style cả 3, không sửa (dùng trước khi push cho khớp CI)
#   make format-cpp     / lint-cpp      chỉ C++ (clang-format: core/ platform/ client/)
#   make format-kotlin  / lint-kotlin   chỉ Kotlin (ktlint: client/android)
#   make format-swift   / lint-swift    chỉ Swift (swiftformat: client/ios + client/macos)
#
#   make clean

# Đặt TRƯỚC mọi include để `make` không tham số vẫn là build debug (goal mặc định của
# GNU make là target ĐẦU TIÊN nó gặp — nếu để sau thì `debug` trong core.mk sẽ chiếm chỗ).
all: debug

include make/toolchain.mk
include make/core.mk
include make/windows.mk
include make/macos.mk
include make/linux.mk
include make/ios.mk
include make/android.mk
include make/codestyle.mk

# Cài toàn bộ dependency dev (idempotent — có rồi thì bỏ qua).
bootstrap:
	@$(BOOTSTRAP)

clean:
	@$(RMRF) out

.PHONY: all bootstrap clean
