# make/macos.mk — app macOS: build-macos / release-macos / run-macos / dist-macos.
#
# MỘT app chứa cả vai host lẫn vai client, build bằng xcodebuild.
# Sản phẩm ra out/build/macos/<Config>/app.app.
# LƯU Ý khi chạy thử: app cần quyền Screen Recording (vai host) và Accessibility (cho
# điều khiển từ xa) — xem docs/14-macos-app.md §5.
#
# BA CHẾ ĐỘ KÝ (MACOS_SIGN)
#   (trống)      — cấu hình trong project: "Apple Development" của máy dev. macOS nhớ
#                  quyền Screen Recording/Accessibility theo MỘT chữ ký ổn định, nên
#                  đây là chế độ để phát triển hằng ngày.
#   adhoc        — ép ký ad-hoc: build được ở mọi nơi kể cả CI không có private key.
#                  App vẫn chạy sau khi người tải tự bỏ quarantine. NHƯNG chữ ký đổi
#                  sau mỗi build → TCC hỏi lại quyền mỗi lần cập nhật. Chỉ dùng cho
#                  build kiểm tra trên CI (.github/workflows/build.yml).
#   developerid  — ký bằng "Developer ID Application" để PHÁT HÀNH. Chữ ký ổn định
#                  vĩnh viễn → cấp quyền một lần là xong. Đi kèm notarize (dist-macos).
#
# PHÁT HÀNH THẬT: `make dist-macos` — build + notarize + staple + đóng dmg. Xem
# docs/16-release-macos.md. Không lên được Mac App Store vì app tắt sandbox (lý do
# nằm trong client/macos/app/Deskhub.entitlements). Người dùng Mac muốn bản trên
# store thì có app iOS chạy theo diện "Designed for iPad" — chỉ có vai client.

ifeq ($(UNAME),Darwin)
MACOS_PROJ := client/macos/Deskhub.xcodeproj
MACOS_OUT  := $(CURDIR)/out/build/macos
MACOS_APP  := $(MACOS_OUT)/Release/app.app
MACOS_TEAM ?= UPJRMYQ38F

# Thư mục dàn dựng + sản phẩm phát hành.
MACOS_DIST    := $(CURDIR)/out/dist/macos
MACOS_ZIP     := $(MACOS_DIST)/Deskhub.zip
MACOS_DMG     := $(MACOS_DIST)/Deskhub.dmg
MACOS_DMG_SRC := $(MACOS_DIST)/dmg

ifeq ($(MACOS_SIGN),adhoc)
MACOS_SIGN_FLAGS := CODE_SIGN_IDENTITY=- CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM=
endif
ifeq ($(MACOS_SIGN),developerid)
# Hai flag cuối là bắt buộc để qua notarization khi build bằng `xcodebuild build`
# thường (không phải archive/export): mặc định Xcode inject entitlement debug
# com.apple.security.get-task-allow và ký với --timestamp=none — Apple từ chối
# cả hai (status: Invalid).
MACOS_SIGN_FLAGS := CODE_SIGN_IDENTITY="Developer ID Application" CODE_SIGN_STYLE=Manual \
                    DEVELOPMENT_TEAM=$(MACOS_TEAM) \
                    CODE_SIGN_INJECT_BASE_ENTITLEMENTS=NO \
                    OTHER_CODE_SIGN_FLAGS=--timestamp
endif

# Tham số xcodebuild bơm từ ngoài — fastlane dùng để ĐÈ version (MARKETING_VERSION từ
# file VERSION ở gốc repo, CURRENT_PROJECT_VERSION từ BUILD_NUMBER của CI), đúng cách
# client/ios/fastlane/Fastfile làm cho bản iOS. pbxproj chỉ còn là fallback khi build
# tay bằng Xcode.
MACOS_XCARGS ?=

# Thông tin đăng nhập notarytool — CÙNG BỘ App Store Connect API key đang dùng cho
# iOS/TestFlight (xem client/ios/fastlane/Fastfile), không cần secret riêng.
# ASC_KEY_P8 là ĐƯỜNG DẪN tới file .p8; CI giải base64 từ secret ASC_KEY_CONTENT ra
# $RUNNER_TEMP rồi truyền vào đây (xem .github/workflows/deploy.yml).
NOTARY_CREDS := --key "$(ASC_KEY_P8)" --key-id "$(ASC_KEY_ID)" --issuer "$(ASC_ISSUER_ID)"

# Nộp $(1) cho Apple và chờ quét xong. notarytool exit 0 KỂ CẢ khi bị từ chối
# (status: Invalid) — không tự kiểm tra thì make chạy tiếp và chết mù ở bước
# staple ("Record not found"). Nên: đọc status từ output; nếu không phải Accepted
# thì kéo log thẩm định của Apple về in ra (lý do từ chối nằm trong đó) rồi dừng.
define notarize
	xcrun notarytool submit $(1) --wait $(NOTARY_CREDS) | tee $(MACOS_DIST)/notary.txt; \
	grep -q 'status: Accepted' $(MACOS_DIST)/notary.txt || { \
	  sub_id=$$(awk '/^  id:/{print $$2; exit}' $(MACOS_DIST)/notary.txt); \
	  echo "notarize: rejected, fetching Apple log for $$sub_id"; \
	  xcrun notarytool log "$$sub_id" $(NOTARY_CREDS); exit 1; }
endef

build-macos:
	xcodebuild -project $(MACOS_PROJ) -target app -configuration Debug SYMROOT=$(MACOS_OUT) $(MACOS_SIGN_FLAGS) $(MACOS_XCARGS) build

release-macos:
	xcodebuild -project $(MACOS_PROJ) -target app -configuration Release SYMROOT=$(MACOS_OUT) $(MACOS_SIGN_FLAGS) $(MACOS_XCARGS) build

run-macos: build-macos
	open out/build/macos/Debug/app.app

# --- Phát hành: build ký Developer ID -> notarize -> staple -> dmg --------------
#
# VÌ SAO NOTARIZE HAI LẦN. Ta NỘP file nén nhưng chỉ STAPLE được vào .app hoặc .dmg
# (không staple được vào .zip — zip chỉ là phương tiện vận chuyển). Nên: nộp zip để
# lấy vé cho .app, staple .app, rồi đóng .app đã staple vào dmg và notarize/staple
# nốt cái dmg. Có vé dán sẵn thì máy người dùng xác thực được cả khi offline.
dist-macos:
	@test -n "$(ASC_KEY_P8)" || { echo "dist-macos: thiếu ASC_KEY_P8 (đường dẫn file .p8)"; exit 1; }
	$(MAKE) release-macos MACOS_SIGN=developerid MACOS_XCARGS="$(MACOS_XCARGS)"
	mkdir -p $(MACOS_DIST)
	# 1. Nộp .app (bọc trong zip) và chờ Apple quét xong.
	ditto -c -k --keepParent $(MACOS_APP) $(MACOS_ZIP)
	$(call notarize,$(MACOS_ZIP))
	# 2. Dán vé vào chính .app.
	xcrun stapler staple $(MACOS_APP)
	# 3. Dàn dmg: app đã staple + lối tắt /Applications để kéo-thả cài.
	rm -rf $(MACOS_DMG_SRC) $(MACOS_DMG)
	mkdir -p $(MACOS_DMG_SRC)
	cp -R $(MACOS_APP) $(MACOS_DMG_SRC)/Deskhub.app
	ln -s /Applications $(MACOS_DMG_SRC)/Applications
	hdiutil create -volname Deskhub -srcfolder $(MACOS_DMG_SRC) -ov -format UDZO $(MACOS_DMG)
	# 4. dmg cũng phải ký + notarize + staple thì Gatekeeper mới nhận chính file tải về.
	codesign --sign "Developer ID Application" --timestamp $(MACOS_DMG)
	$(call notarize,$(MACOS_DMG))
	xcrun stapler staple $(MACOS_DMG)
	@echo "dist-macos: xong -> $(MACOS_DMG)"

# Kiểm tra đúng thứ máy người dùng sẽ thấy. Kỳ vọng: "accepted" + "source=Notarized
# Developer ID". Chạy được trên bất kỳ Mac nào, kể cả máy không có cert.
verify-macos:
	xcrun stapler validate $(MACOS_APP)
	spctl -a -vvv -t install $(MACOS_APP)
else
build-macos release-macos run-macos dist-macos verify-macos:
	@echo "make $@: needs macOS + Xcode"; exit 1
endif

.PHONY: build-macos release-macos run-macos dist-macos verify-macos
