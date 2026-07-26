// =============================================================================
// TestMain.cpp — gom mọi nhóm test lại và chạy. Exit 0 = tất cả check đạt.
//
// Test của core chạy hoàn toàn offline (không mạng, không GPU) nên build/chạy được
// bằng MỌI toolchain dựng được core — MSVC, clang/gcc, Android NDK.
//
// Chạy:  cmake --build --preset x64-debug --target core_tests && ./core_tests
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include <cstdio>

int main() {
    std::printf("=== core self-test (offline: no network, no GPU) ===\n");

    std::printf("--- wire ---\n");
    RunWireTests();

    // Crypto đi TRƯỚC auth: nếu SHA-256/HMAC/PBKDF2 sai thì mọi ca xác thực bên dưới
    // đều vô nghĩa, và đọc log từ trên xuống sẽ thấy ngay nguyên nhân gốc.
    std::printf("--- crypto: SHA-256 / HMAC / PBKDF2 (standard vectors) ---\n");
    RunCryptoTests();

    std::printf("--- auth: password gate, lockout, trusted devices ---\n");
    RunAuthTests();

    std::printf("--- transport: reassembler ---\n");
    RunReassemblerTests();

    std::printf("--- transport: FEC ---\n");
    RunFecTests();

    std::printf("--- transport: retransmit/NACK ---\n");
    RunRetransmitCacheTests();

    std::printf("--- session ---\n");
    RunSessionTests();

    std::printf("--- input ---\n");
    RunInputTests();

    std::printf("--- control: bitrate + link stats + latency ---\n");
    RunControlTests();

    std::printf("--- discovery: beacon + host registry ---\n");
    RunDiscoveryTests();

    if (g_failures == 0) {
        std::printf("=== PASS: all checks passed ===\n");
        return 0;
    }
    std::printf("=== FAIL: %d checks failed ===\n", g_failures);
    return 1;
}
