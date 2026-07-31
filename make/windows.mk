ifeq ($(OS),Windows_NT)
APP_EXE_DEBUG   := out\build\x64-debug\client\windows\win32\Deskhub.exe
APP_EXE_RELEASE := out\build\x64-release\client\windows\win32\Deskhub.exe

build-windows: debug

release-windows: release

run-windows: build-windows
	$(APP_EXE_DEBUG) $(ARGS)
else
build-windows release-windows run-windows:
	@echo "make $@: run on a Windows machine (the Windows app needs MSVC)"; exit 1
endif

.PHONY: build-windows release-windows run-windows
