ifeq ($(OS),Windows_NT)
CORE_TESTS := out\build\x64-debug\core\core_tests.exe
PLAT_TESTS := out\build\x64-debug\platform\platform_tests.exe
COV_TESTS  := out\build\coverage\core\core_tests.exe
COV_RAW    := out\build\coverage\core_tests.profraw
COV_DATA   := out\build\coverage\core_tests.profdata
COV_OUT    := out\coverage
COV_SRC    := core\src core\include
else
CORE_TESTS := out/build/x64-debug/core/core_tests
PLAT_TESTS := out/build/x64-debug/platform/platform_tests
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

test:
	@$(DEVCMD) cmake --preset x64-debug >$(NULDEV) && cmake --build --preset x64-debug --target core_tests
	@echo ===== Running core_tests offline =====
	$(CORE_TESTS)

test-platform:
	@$(DEVCMD) cmake --preset x64-debug >$(NULDEV) && cmake --build --preset x64-debug --target platform_tests
	@echo ===== Running platform_tests locally =====
	$(PLAT_TESTS)

test-all: test test-platform

test-ctest:
	@$(DEVCMD) cmake --preset x64-debug >$(NULDEV) && cmake --build --preset x64-debug --target core_tests
	@$(DEVCMD) cmake --build --preset x64-debug --target platform_tests
	@$(DEVCMD) ctest --test-dir out/build/x64-debug --output-on-failure

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

.PHONY: debug release test test-platform test-all test-ctest coverage
