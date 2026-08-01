#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhubp/ffi/FfiText.h"

#include <cstdio>
#include <cstring>
#include <string>

using deskhubp::CopyToBuf;

namespace {

void TestAShortStringCrossesIntact() {
    std::printf("[ffi] a status line short enough to fit crosses the boundary whole...\n");
    char buf[32];
    std::memset(buf, 0x7F, sizeof(buf));
    CopyToBuf(buf, sizeof(buf), "60fps  12ms");
    Check(std::strcmp(buf, "60fps  12ms") == 0, "the text arrives unchanged");
    Check(buf[11] == '\0', "and is terminated right after it");
    Check(buf[12] == 0x7F, "the rest of the buffer is left alone, not needlessly cleared");
}

void TestALongStringIsTruncatedNotOverflowed() {
    std::printf("[ffi] a status line that is too long is cut, never written past the end...\n");
    char raw[24];
    std::memset(raw, 0x5A, sizeof(raw));
    char* buf = raw + 4;
    constexpr size_t kCap = 9;

    CopyToBuf(buf, kCap, std::string(100, 'x'));
    Check(std::strlen(buf) == kCap - 1, "exactly cap-1 characters are kept");
    Check(buf[kCap - 1] == '\0', "the last byte is always the terminator");
    Check(raw[3] == 0x5A && raw[4 + kCap] == 0x5A,
        "and nothing on either side of the buffer was touched");
}

void TestTheExactFitIsHandled() {
    std::printf("[ffi] a string that exactly fills the buffer still gets its terminator...\n");
    char buf[6];
    CopyToBuf(buf, sizeof(buf), "12345");
    Check(std::strcmp(buf, "12345") == 0, "cap-1 characters fit exactly");

    CopyToBuf(buf, sizeof(buf), "123456");
    Check(std::strcmp(buf, "12345") == 0, "one more character is dropped, not the terminator");
}

void TestDegenerateBuffersAreRefused() {
    std::printf("[ffi] a missing or zero-length buffer is ignored instead of crashing...\n");
    CopyToBuf(nullptr, 32, "anything");

    char buf[4] = {'a', 'b', 'c', '\0'};
    CopyToBuf(buf, 0, "anything");
    Check(std::strcmp(buf, "abc") == 0, "a zero capacity leaves the buffer as it was");

    CopyToBuf(buf, 1, "anything");
    Check(buf[0] == '\0', "a one-byte buffer can only hold the terminator");
}

void TestAnEmptyStringClearsTheBuffer() {
    std::printf("[ffi] an empty end reason reads as empty, not as the previous one...\n");
    char buf[16];
    CopyToBuf(buf, sizeof(buf), "disconnected");
    CopyToBuf(buf, sizeof(buf), "");
    Check(buf[0] == '\0', "the stale text is no longer readable through the pointer");
}

void TestEmbeddedNulsDoNotShortenTheCopy() {
    std::printf("[ffi] the byte count copied is the string's own length...\n");
    char buf[16];
    std::memset(buf, 0x11, sizeof(buf));
    std::string s("ab");
    s.push_back('\0');
    s += "cd";
    CopyToBuf(buf, sizeof(buf), s);
    Check(std::memcmp(buf, "ab\0cd", 5) == 0, "all five bytes are copied");
    Check(buf[5] == '\0', "and the terminator goes after the whole string");
}

}

void RunFfiTextTests() {
    TestAShortStringCrossesIntact();
    TestALongStringIsTruncatedNotOverflowed();
    TestTheExactFitIsHandled();
    TestDegenerateBuffersAreRefused();
    TestAnEmptyStringClearsTheBuffer();
    TestEmbeddedNulsDoNotShortenTheCopy();
}
