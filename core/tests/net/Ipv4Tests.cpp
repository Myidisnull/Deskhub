#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/net/Ipv4.h"

#include <cstdio>

using namespace deskhub;

namespace {

void TestValidAddresses() {
    std::printf("[ipv4] real dotted-quad addresses parse to the packed value...\n");
    Check(ParseIPv4("0.0.0.0") == 0u, "all zeros parses");
    Check(ParseIPv4("127.0.0.1") == 0x7F000001u, "loopback parses");
    Check(ParseIPv4("192.168.1.10") == 0xC0A8010Au, "a LAN address parses");
    Check(ParseIPv4("255.255.255.255") == 0xFFFFFFFFu, "the top of the range parses");
    Check(ParseIPv4("10.0.0.1") == 0x0A000001u, "a short-octet address parses");
}

void TestInvalidAddresses() {
    std::printf("[ipv4] everything else is rejected...\n");
    Check(!ParseIPv4(""), "empty text is not an address");
    Check(!ParseIPv4("192.168.1"), "three octets are not enough");
    Check(!ParseIPv4("192.168.1.10.5"), "five octets are too many");
    Check(!ParseIPv4("192.168.1.256"), "an octet past 255 is rejected");
    Check(!ParseIPv4("192.168.1.1000"), "a four-digit octet is rejected");
    Check(!ParseIPv4("192.168..1"), "an empty octet is rejected");
    Check(!ParseIPv4(".192.168.1.1"), "a leading dot is rejected");
    Check(!ParseIPv4("192.168.1.1."), "a trailing dot is rejected");
    Check(!ParseIPv4("192.168.1.1 "), "trailing junk is rejected");
    Check(!ParseIPv4("192.168.one.1"), "letters are rejected");
    Check(!ParseIPv4("-1.0.0.0"), "a sign is rejected");
    Check(!ParseIPv4("192,168,1,1"), "commas are not dots");
}

}

void RunIpv4Tests() {
    TestValidAddresses();
    TestInvalidAddresses();
}
