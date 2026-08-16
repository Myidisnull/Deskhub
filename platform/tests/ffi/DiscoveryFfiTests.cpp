#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhubp/ffi/DiscoveryFfi.h"
#include "deskhubp/net/UdpSocket.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

void TestFormatAddressFillsAndSizesLikeEveryFfiString() {
    std::printf("[discffi] one string contract: a null buffer sizes, a small one truncates...\n");
    const uint64_t packed = NetAddr{0xC0A8010Au, 47901}.Pack();
    const std::string expected = "192.168.1.10:47901";

    Check(dh_format_address(packed, nullptr, 0) == int(expected.size()),
        "no buffer asks how much room the caller needs");

    char exact[19] = {};
    Check(dh_format_address(packed, exact, sizeof(exact)) == int(expected.size()) &&
              expected == exact,
        "a buffer that fits gets the whole address");

    char tiny[8] = {};
    tiny[7] = 'x';
    const int wrote = dh_format_address(packed, tiny, sizeof(tiny));
    Check(wrote == 7 && std::strncmp(tiny, expected.c_str(), 7) == 0 && tiny[7] == '\0',
        "a small buffer is truncated and always terminated, never overrun");
}

}

void RunDiscoveryFfiTests() {
    std::printf("--- ffi: the discovery surface the device lists poll ---\n");
    TestFormatAddressFillsAndSizesLikeEveryFfiString();
}
