#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/session/ClientReconnect.h"

#include <cstdio>

using namespace deskhub;

namespace {

void TestTransientReasons() {
    std::printf("[reconnect] transient disconnect reasons are recognised...\n");
    Check(IsTransientClientDisconnect("lost contact with host (timeout)"), "timeout");
    Check(IsTransientClientDisconnect("socket error"), "socket");
    Check(IsTransientClientDisconnect("could not connect (timed out)"), "hello timeout");
    Check(!IsTransientClientDisconnect("host ended the session (BYE)"), "BYE is final");
    Check(!IsTransientClientDisconnect("wrong passcode — check the 4-digit code on the host"),
        "auth is final");
    Check(!IsTransientClientDisconnect("stopped"), "user stop is final");
    Check(!IsTransientClientDisconnect(""), "empty is not transient");
}

void TestBackoffGrowsAndCaps() {
    std::printf("[reconnect] backoff doubles then caps...\n");
    Check(ClientReconnectBackoffUs(0) == kClientReconnectBackoffUs, "first wait");
    Check(ClientReconnectBackoffUs(1) == kClientReconnectBackoffUs * 2, "second wait");
    Check(ClientReconnectBackoffUs(2) == kClientReconnectBackoffUs * 4, "third wait");
    Check(ClientReconnectBackoffUs(10) == kClientReconnectBackoffCapUs, "cap holds");
    Check(ClientReconnectBackoffUs(100) == kClientReconnectBackoffCapUs, "cap stays");
}

}

void RunClientReconnectTests() {
    TestTransientReasons();
    TestBackoffGrowsAndCaps();
}
