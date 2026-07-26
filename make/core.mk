# make/core.mk — cây CMake của lõi, dùng chung cho MỌI nền tảng và chạy được trên cả
# Windows/macOS/Ubuntu: debug, release, test, test-ctest, coverage.
# Đây là phần không thuộc riêng nền tảng nào; app từng nền tảng nằm ở make/<nền tảng>.mk.

ifeq ($(OS),Windows_NT)
CORE_TESTS := out\build\x64-debug\core\core_tests.exe
COV_TESTS  := out\build\coverage\core\core_tests.exe
COV_RAW    := out\build\coverage\core_tests.profraw
COV_DATA   := out\build\coverage\core_tests.profdata
COV_OUT    := out\coverage
COV_SRC    := core\src core\include
else
CORE_TESTS := out/build/x64-debug/core/core_tests
COV_TESTS  := out/build/coverage/core/core_tests
COV_RAW    := out/build/coverage/core_tests.profraw
COV_DATA   := out/build/coverage/core_tests.profdata
COV_OUT    := out/coverage
COV_SRC    := core/src core/include
endif

debug:
	@$(DEVCMD) cmake --preset x64-debug && cmake --build --preset x64-debug

release:
	@$(DEVCMD) cmake --preset x64-release && cmake --build --preset x64-release

# Test của core: offline, không cần mạng/GPU. Chỉ build target core_tests (không dựng
# client) nên nhanh. Exit code 0 = pass.
test:
	@$(DEVCMD) cmake --preset x64-debug >$(NULDEV) && cmake --build --preset x64-debug --target core_tests
	@echo ===== Running core_tests offline =====
	$(CORE_TESTS)

# Chạy qua CTest — cùng cách CI chạy. --output-on-failure in stdout của test khi rớt.
test-ctest:
	@$(DEVCMD) cmake --preset x64-debug >$(NULDEV) && cmake --build --preset x64-debug --target core_tests
	@$(DEVCMD) ctest --test-dir out/build/x64-debug --output-on-failure

# Đo phủ code của lõi — cùng một cách trên cả 3 OS: build cây riêng preset
# `coverage` bằng clang (instrument -fprofile-instr-generate/-fcoverage-mapping),
# chạy core_tests sinh .profraw rồi xuất báo cáo qua llvm-profdata + llvm-cov.
# Nguồn tool: Windows = LLVM kèm VS (VC.Llvm.Clang), macOS = Xcode (xcrun),
# Ubuntu = gói clang + llvm. Chỉ tính core/src + core/include (positional filter
# của llvm-cov) nên code test tự động nằm ngoài mẫu số.
# Khác nhau giữa 2 nhánh chỉ là cách đặt biến môi trường LLVM_PROFILE_FILE và
# thứ tự $(LLVMPATH) $(DEVCMD) — xem ghi chú ở make/toolchain.mk.
ifeq ($(OS),Windows_NT)
coverage:
	@$(LLVMPATH) $(DEVCMD) cmake --preset coverage >$(NULDEV) && cmake --build --preset coverage --target core_tests
	@$(DEVCMD) set "LLVM_PROFILE_FILE=$(COV_RAW)" && $(COV_TESTS)
	@$(LLVMPATH) $(DEVCMD) llvm-profdata merge -sparse $(COV_RAW) -o $(COV_DATA)
	@$(LLVMPATH) $(DEVCMD) llvm-cov show $(COV_TESTS) -instr-profile=$(COV_DATA) -format=html -output-dir=$(COV_OUT) $(COV_SRC)
	@$(LLVMPATH) $(DEVCMD) llvm-cov report $(COV_TESTS) -instr-profile=$(COV_DATA) $(COV_SRC)
	@echo Report: $(COV_OUT)\index.html
else
coverage:
	@cmake --preset coverage >$(NULDEV) && cmake --build --preset coverage --target core_tests
	LLVM_PROFILE_FILE=$(COV_RAW) $(COV_TESTS)
	@$(LLVM) llvm-profdata merge -sparse $(COV_RAW) -o $(COV_DATA)
	@$(LLVM) llvm-cov show $(COV_TESTS) -instr-profile=$(COV_DATA) -format=html -output-dir=$(COV_OUT) $(COV_SRC)
	@$(LLVM) llvm-cov report $(COV_TESTS) -instr-profile=$(COV_DATA) $(COV_SRC)
	@echo "Report: $(COV_OUT)/index.html"
endif

.PHONY: debug release test test-ctest coverage
