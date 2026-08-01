#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/protocol/ByteOrder.h"

#include <cstdio>
#include <cstring>

using namespace deskhub;

namespace {

void TestBigEndianLayout() {
    std::printf("[byteorder] every field goes on the wire most-significant byte first...\n");

    uint8_t b[8] = {};
    PutU16(b, 0x1234);
    Check(b[0] == 0x12 && b[1] == 0x34, "u16 is written big-endian");

    PutU32(b, 0x12345678u);
    Check(b[0] == 0x12 && b[1] == 0x34 && b[2] == 0x56 && b[3] == 0x78,
        "u32 is written big-endian");

    PutU64(b, 0x0123456789ABCDEFull);
    Check(b[0] == 0x01 && b[1] == 0x23 && b[2] == 0x45 && b[3] == 0x67 && b[4] == 0x89 &&
              b[5] == 0xAB && b[6] == 0xCD && b[7] == 0xEF,
        "u64 is written big-endian");
}

void TestReadersMatchTheWriters() {
    std::printf("[byteorder] what a host writes is what any host reads back...\n");

    uint8_t b[8] = {};
    const uint16_t v16[] = {0, 1, 0x00FF, 0x0100, 0x7FFF, 0x8000, 0xFFFF};
    for (uint16_t v : v16) {
        PutU16(b, v);
        Check(GetU16(b) == v, "u16 round-trips including the sign-bit boundary");
    }

    const uint32_t v32[] = {0, 1, 0x0000FFFFu, 0x00010000u, 0x7FFFFFFFu, 0x80000000u,
        0xFFFFFFFFu};
    for (uint32_t v : v32) {
        PutU32(b, v);
        Check(GetU32(b) == v, "u32 round-trips including the high-bit boundary");
    }

    const uint64_t v64[] = {0, 1, 0x00000000FFFFFFFFull, 0x0000000100000000ull,
        0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull};
    for (uint64_t v : v64) {
        PutU64(b, v);
        Check(GetU64(b) == v, "u64 round-trips across the 32-bit split");
    }
}

void TestNeighbouringBytesAreLeftAlone() {
    std::printf("[byteorder] a write touches exactly its own field, never the next one...\n");

    uint8_t b[16];
    std::memset(b, 0xAA, sizeof(b));
    PutU16(b + 4, 0xFFFF);
    Check(b[3] == 0xAA && b[6] == 0xAA, "u16 writes 2 bytes");

    std::memset(b, 0xAA, sizeof(b));
    PutU32(b + 4, 0xFFFFFFFFu);
    Check(b[3] == 0xAA && b[8] == 0xAA, "u32 writes 4 bytes");

    std::memset(b, 0xAA, sizeof(b));
    PutU64(b + 4, 0xFFFFFFFFFFFFFFFFull);
    Check(b[3] == 0xAA && b[12] == 0xAA, "u64 writes 8 bytes");
}

void TestUnalignedAccessIsFine() {
    std::printf("[byteorder] fields sitting at odd offsets read back the same...\n");

    uint8_t b[24] = {};
    for (size_t off = 0; off < 8; ++off) {
        PutU64(b + off, 0x0123456789ABCDEFull);
        Check(GetU64(b + off) == 0x0123456789ABCDEFull,
            "a u64 at an unaligned offset survives the round-trip");
        PutU32(b + off + 8, 0xDEADBEEFu);
        Check(GetU32(b + off + 8) == 0xDEADBEEFu,
            "a u32 at an unaligned offset survives the round-trip");
    }
}

}

void RunByteOrderTests() {
    TestBigEndianLayout();
    TestReadersMatchTheWriters();
    TestNeighbouringBytesAreLeftAlone();
    TestUnalignedAccessIsFine();
}
