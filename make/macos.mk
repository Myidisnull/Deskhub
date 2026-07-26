# make/macos.mk — app macOS: build-macos / release-macos / run-macos.
#
# MỘT app chứa cả vai host lẫn vai client (kiểu AnyDesk), build bằng xcodebuild.
# Sản phẩm ra out/build/macos/<Config>/app.app. Ký ad-hoc (CODE_SIGN_IDENTITY = "-")
# nên chạy được ngay trên máy dev; bản phát hành cần Developer ID + notarize.
# LƯU Ý khi chạy thử: app cần quyền Screen Recording (vai host) và Accessibility (cho
# điều khiển từ xa) — xem docs/14-macos-app.md §5.

ifeq ($(UNAME),Darwin)
MACOS_PROJ := client/macos/Deskhub.xcodeproj
MACOS_OUT  := $(CURDIR)/out/build/macos

build-macos:
	xcodebuild -project $(MACOS_PROJ) -target app -configuration Debug SYMROOT=$(MACOS_OUT) build

release-macos:
	xcodebuild -project $(MACOS_PROJ) -target app -configuration Release SYMROOT=$(MACOS_OUT) build

run-macos: build-macos
	open out/build/macos/Debug/app.app
else
build-macos release-macos run-macos:
	@echo "make $@: needs macOS + Xcode"; exit 1
endif

.PHONY: build-macos release-macos run-macos
