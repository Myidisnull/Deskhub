# make/macos.mk — app macOS: build-macos / release-macos / run-macos.
#
# MỘT app chứa cả vai host lẫn vai client (kiểu AnyDesk), build bằng xcodebuild.
# Sản phẩm ra out/build/macos/<Config>/app.app.
# LƯU Ý khi chạy thử: app cần quyền Screen Recording (vai host) và Accessibility (cho
# điều khiển từ xa) — xem docs/14-macos-app.md §5.
#
# Ký: mặc định dùng cấu hình trong project — "Apple Development" của máy dev, để macOS
# nhớ quyền Screen Recording/Accessibility theo MỘT chữ ký ổn định (ký ad-hoc mỗi build
# ra một chữ ký khác → TCC hỏi quyền lại liên tục). CI không có private key của team →
# gọi `make ... MACOS_SIGN=adhoc` để ép ký ad-hoc: build được ở mọi nơi, app vẫn chạy
# sau khi người tải tự bỏ quarantine; bản phát hành thật cần Developer ID + notarize.

ifeq ($(UNAME),Darwin)
MACOS_PROJ := client/macos/Deskhub.xcodeproj
MACOS_OUT  := $(CURDIR)/out/build/macos

ifeq ($(MACOS_SIGN),adhoc)
MACOS_SIGN_FLAGS := CODE_SIGN_IDENTITY=- CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM=
endif

build-macos:
	xcodebuild -project $(MACOS_PROJ) -target app -configuration Debug SYMROOT=$(MACOS_OUT) $(MACOS_SIGN_FLAGS) build

release-macos:
	xcodebuild -project $(MACOS_PROJ) -target app -configuration Release SYMROOT=$(MACOS_OUT) $(MACOS_SIGN_FLAGS) build

run-macos: build-macos
	open out/build/macos/Debug/app.app
else
build-macos release-macos run-macos:
	@echo "make $@: needs macOS + Xcode"; exit 1
endif

.PHONY: build-macos release-macos run-macos
