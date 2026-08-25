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

void TestGraceWindow() {
    std::printf("[reconnect] the grace window keeps trying until it runs out...\n");
    Check(ClientReconnectStillWorthTrying(0), "a fresh loss is still worth trying");
    Check(ClientReconnectStillWorthTrying(kClientReconnectGraceUs - 1),
        "just inside the grace still tries");
    Check(!ClientReconnectStillWorthTrying(kClientReconnectGraceUs),
        "at the grace limit we stop");
    Check(!ClientReconnectStillWorthTrying(kClientReconnectGraceUs + 1),
        "past the grace we stop");
    Check(ClientReconnectStillWorthTrying(100, 200), "a custom grace still applies");
    Check(!ClientReconnectStillWorthTrying(200, 200), "and ends at its own limit");
}

}

void RunClientReconnectTests() {
    TestTransientReasons();
    TestBackoffGrowsAndCaps();
    TestGraceWindow();
}
