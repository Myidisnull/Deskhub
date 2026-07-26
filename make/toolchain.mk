# make/toolchain.mk — biến phụ thuộc HOST (máy đang chạy make), KHÔNG phải nền tảng đích.
# Phải được include ĐẦU TIÊN: các file nền tảng khác dùng $(DEVCMD)/$(NULDEV)/$(UNAME).
#
# Ở đây chỉ để thứ dùng chung nhiều nền tảng. Biến riêng của một nền tảng (đường dẫn
# .csproj, gradlew, adb, ...) nằm cạnh target của nó trong make/<nền tảng>.mk.

ifeq ($(OS),Windows_NT)
# --- Windows: mọi lệnh cmake/ctest đi qua VsDevCmd để có MSVC + CMake + Ninja của VS.
SHELL := cmd.exe
.SHELLFLAGS := /c

# -products * là BẮT BUỘC: mặc định vswhere bỏ qua BuildTools, máy chỉ cài BuildTools
# sẽ ra chuỗi rỗng. Giống scripts/bootstrap.ps1 và scripts/codestyle.ps1.
VSWHERE := C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe
VSDIR   := $(shell "$(VSWHERE)" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
VSDEV   := $(VSDIR)\Common7\Tools\VsDevCmd.bat
DEVCMD  := call "$(VSDEV)" -arch=x64 -host_arch=x64 -no_logo &&

# LLVM đi kèm VS (component VC.Llvm.Clang — bootstrap cài): clang++ + llvm-cov/profdata.
# VsDevCmd không tự thêm thư mục này vào PATH nên coverage tự prepend.
# QUAN TRỌNG: phải đặt TRƯỚC $(DEVCMD), tức là "$(LLVMPATH) $(DEVCMD) <lệnh>".
# cmd.exe khai triển %PATH% cho CẢ chuỗi && ngay lúc parse, nên nếu đặt sau VsDevCmd
# thì %PATH% ở đây là PATH CŨ — set sẽ xóa sạch phần VsDevCmd vừa thêm (mất cmake/ninja).
# Đặt trước thì VsDevCmd prepend tiếp vào PATH đã có LLVM nên cả hai cùng dùng được.
LLVMPATH := set "PATH=$(VSDIR)\VC\Tools\Llvm\x64\bin;%PATH%" &&
LLVM     :=

BOOTSTRAP := powershell -NoProfile -ExecutionPolicy Bypass -File scripts\bootstrap.ps1
NULDEV    := nul
RMRF      := $(DEVCMD) cmake -E rm -rf
else
# --- macOS/Ubuntu: cmake/ninja/clang(gcc) lấy thẳng từ PATH (scripts/bootstrap.sh cài).
UNAME  := $(shell uname -s)
DEVCMD :=

# macOS: llvm-cov/llvm-profdata nằm trong toolchain Xcode, gọi qua xcrun.
# Ubuntu: gói llvm cài thẳng vào PATH (bootstrap.sh cài clang + llvm).
LLVMPATH :=
LLVM     := $(if $(filter Darwin,$(UNAME)),xcrun)

BOOTSTRAP := scripts/bootstrap.sh
NULDEV    := /dev/null
RMRF      := rm -rf
endif
