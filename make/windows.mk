# make/windows.mk — app Windows: build-windows / release-windows / run.
#
# App Windows là MỘT exe Win32 thuần (client/windows/win32, target CMake
# deskhub_app -> Deskhub.exe) chứa CẢ vai host lẫn vai client. `make debug`/
# `release` (core.mk) đã dựng nó cùng cây CMake; các target ở đây chỉ là bí
# danh + đường chạy. (Hai frontend cũ csharp/WinUI3 + imgui đã xoá 2026-07-27.)

ifeq ($(OS),Windows_NT)
APP_EXE_DEBUG   := out\build\x64-debug\client\windows\win32\Deskhub.exe
APP_EXE_RELEASE := out\build\x64-release\client\windows\win32\Deskhub.exe

build-windows: debug

release-windows: release

run: build-windows
	$(APP_EXE_DEBUG) $(ARGS)
else
# Trên máy không phải Windows thì báo lỗi thay vì build nhầm core.
build-windows release-windows run:
	@echo "make $@: run on a Windows machine (desktop client is Windows-only for now)"; exit 1
endif

.PHONY: build-windows release-windows run
