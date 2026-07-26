# make/windows.mk — app Windows: build-windows / release-windows / run.
#
# App Windows = 2 lớp: CMake dựng deskhub_native.dll, dotnet/MSBuild dựng Deskhub.exe
# (giống client/android: cpp/ qua CMake, java/ qua Gradle). Deskhub.exe chứa CẢ vai host
# lẫn vai client (kiểu AnyDesk) — từ M4b đây là app Windows duy nhất.
# .csproj tự chép DLL native theo cấu hình: Release lấy preset x64-release, còn lại x64-debug.

ifeq ($(OS),Windows_NT)
# Đường ra của .csproj gồm cả TFM lẫn RID nên ghim ở đây; đổi TargetFramework trong
# .csproj thì sửa WINUI_TFM. Build luôn truyền -p:Platform=x64 để ra bin\x64\... —
# cùng chỗ Visual Studio ghi, khỏi lệch (dotnet build không có Platform ghi vào bin\Debug\...).
WINUI_PROJ := client\windows\csharp\Deskhub.csproj
WINUI_TFM  := net9.0-windows10.0.19041.0
WINUI_EXE  := client\windows\csharp\bin\x64\Debug\$(WINUI_TFM)\win-x64\Deskhub.exe

build-windows: debug
	@$(DEVCMD) dotnet build $(WINUI_PROJ) -c Debug -p:Platform=x64

release-windows: release
	@$(DEVCMD) dotnet build $(WINUI_PROJ) -c Release -p:Platform=x64

run: build-windows
	$(WINUI_EXE) $(ARGS)
else
# Trên máy không phải Windows thì báo lỗi thay vì build nhầm core.
build-windows release-windows run:
	@echo "make $@: run on a Windows machine (desktop client is Windows-only for now)"; exit 1
endif

.PHONY: build-windows release-windows run
