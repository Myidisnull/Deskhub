ifeq ($(UNAME),Linux)
LINUX_APP_DEBUG   := out/build/x64-debug/client/linux/deskhub
LINUX_APP_RELEASE := out/build/x64-release/client/linux/deskhub

ffmpeg-min:
	@scripts/build-ffmpeg.sh

build-linux: ffmpeg-min
	@cmake --preset x64-debug -DDESKHUB_LINUX_APP=ON -DDESKHUB_REQUIRE_LINUX_APP=ON && cmake --build --preset x64-debug --target deskhub_app

release-linux: ffmpeg-min
	@cmake --preset x64-release -DDESKHUB_LINUX_APP=ON -DDESKHUB_REQUIRE_LINUX_APP=ON && cmake --build --preset x64-release --target deskhub_app

run-linux: build-linux
	$(LINUX_APP_DEBUG) $(ARGS)

setup-linux-permissions:
	@echo 'KERNEL=="uinput", MODE="0660", GROUP="input", OPTIONS+="static_node=uinput"' \
	  | sudo tee /etc/udev/rules.d/60-deskhub-uinput.rules >/dev/null
	@sudo udevadm control --reload-rules && sudo udevadm trigger
	@sudo usermod -aG input "$$USER"
	@echo "setup-linux-permissions: done — LOG OUT and back in for the group change to apply."
else
build-linux release-linux run-linux setup-linux-permissions ffmpeg-min:
	@echo "make $@: needs Ubuntu/Linux"; exit 1
endif

.PHONY: build-linux release-linux run-linux setup-linux-permissions ffmpeg-min
