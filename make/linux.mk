# make/linux.mk — app Ubuntu: build-linux / release-linux / run-linux.
#
# MỘT tệp thực thi `deskhub` chứa cả vai host lẫn vai client, build bằng CMake +
# Ninja trong CÙNG cây với core (khác macOS/iOS dùng xcodebuild và Android dùng
# Gradle — hai hệ đó có build system riêng, còn Linux thì dùng thẳng CMake gốc).
# Sản phẩm ra out/build/x64-{debug,release}/client/linux/deskhub.
#
# QUYỀN CẦN CẤP MỘT LẦN TRƯỚC KHI CHẠY VAI HOST (docs/17-linux-app.md §7)
#   `make setup-linux-permissions` cài quy tắc udev cho /dev/uinput và
#   /dev/input/event* rồi thêm user vào nhóm `input`. Không có nó thì app vẫn
#   chạy và vẫn XEM được, chỉ là không bơm được chuột/bàn phím vào máy này.
#
# LƯU Ý VỀ PHIÊN HIỂN THỊ
#   Vai host cần xdg-desktop-portal của compositor đang dùng
#   (xdg-desktop-portal-gnome / -kde / -wlr). Chạy trên phiên Wayland; phiên Xorg
#   vẫn chạy được qua portal nếu compositor có backend tương ứng.

ifeq ($(UNAME),Linux)
LINUX_APP_DEBUG   := out/build/x64-debug/client/linux/deskhub
LINUX_APP_RELEASE := out/build/x64-release/client/linux/deskhub

build-linux:
	@cmake --preset x64-debug && cmake --build --preset x64-debug --target deskhub_app

release-linux:
	@cmake --preset x64-release && cmake --build --preset x64-release --target deskhub_app

run-linux: build-linux
	$(LINUX_APP_DEBUG) $(ARGS)

# Cấp quyền cho vai host. Tách khỏi `make bootstrap` một cách CÓ CHỦ Ý: bootstrap
# chỉ cài gói dev, còn đây là thay đổi cấu hình hệ thống (udev + nhóm người dùng)
# nên người dùng phải chủ động gọi và biết mình vừa cấp cái gì.
setup-linux-permissions:
	@echo 'KERNEL=="uinput", MODE="0660", GROUP="input", OPTIONS+="static_node=uinput"' \
	  | sudo tee /etc/udev/rules.d/60-deskhub-uinput.rules >/dev/null
	@sudo udevadm control --reload-rules && sudo udevadm trigger
	@sudo usermod -aG input "$$USER"
	@echo "setup-linux-permissions: done — LOG OUT and back in for the group change to apply."
else
build-linux release-linux run-linux setup-linux-permissions:
	@echo "make $@: needs Ubuntu/Linux"; exit 1
endif

.PHONY: build-linux release-linux run-linux setup-linux-permissions
