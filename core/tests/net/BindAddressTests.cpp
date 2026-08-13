#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/net/BindAddress.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

const std::vector<std::string> kAdapters = {"192.168.1.10", "10.0.0.5"};

void TestEmptyMeansAllInterfaces() {
    std::printf("[bind] no preference means all interfaces, no warning...\n");
    const BindChoice c = SelectBindAddress("", kAdapters);
    Check(c.ip.empty(), "no preference binds every interface");
    Check(!c.fellBack, "and that is not a fallback");
    const BindChoice none = SelectBindAddress("", {});
    Check(none.ip.empty() && !none.fellBack, "no adapters at all is still fine");
}

void TestPresentAddressIsChosen() {
    std::printf("[bind] a saved address that still exists is used as-is...\n");
    const BindChoice c = SelectBindAddress("192.168.1.10", kAdapters);
    Check(c.ip == "192.168.1.10", "the saved address is bound");
    Check(!c.fellBack, "no fallback when the address exists");
}

void TestMissingAddressFallsBack() {
    std::printf("[bind] a stale saved address falls back to all interfaces...\n");
    const BindChoice c = SelectBindAddress("172.16.0.9", kAdapters);
    Check(c.ip.empty(), "the stale address is not bound");
    Check(c.fellBack, "and the caller is told to warn the user");
    const BindChoice none = SelectBindAddress("172.16.0.9", {});
    Check(none.ip.empty() && none.fellBack, "no adapters behaves the same way");
}

void TestInvalidAddressFallsBack() {
    std::printf("[bind] junk in the settings file cannot reach the socket...\n");
    const BindChoice c = SelectBindAddress("not-an-ip", kAdapters);
    Check(c.ip.empty(), "junk never becomes a bind address");
    Check(c.fellBack, "and is treated as a fallback");
}

}

void RunBindAddressTests() {
    TestEmptyMeansAllInterfaces();
    TestPresentAddressIsChosen();
    TestMissingAddressFallsBack();
    TestInvalidAddressFallsBack();
}
