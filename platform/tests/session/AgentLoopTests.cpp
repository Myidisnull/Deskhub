#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhubp/session/AgentLoop.h"

#include <cstdio>
#include <vector>

namespace {

PairingRequest Request(uint64_t addr) {
    PairingRequest request;
    request.addrPacked = addr;
    request.shortKey = "shortkey";
    request.name = "machine";
    return request;
}

void TestPairingRequestsSurviveASmallWindow() {
    std::printf("[agentloop] a pairing request never falls off the back of a small window...\n");
    AgentLoop loop;
    for (uint64_t addr = 1; addr <= 3; ++addr) loop.PushPairingRequest(Request(addr));

    const std::vector<PairingRequest> first = loop.TakePairingRequests(2);
    Check(first.size() == 2 && first[0].addrPacked == 1 && first[1].addrPacked == 2,
        "a window of two serves the two oldest requests");

    const std::vector<PairingRequest> second = loop.TakePairingRequests(2);
    Check(second.size() == 1 && second[0].addrPacked == 3,
        "the one that did not fit is still there next time, not dropped");
    Check(second[0].shortKey == "shortkey" && second[0].name == "machine",
        "and it still carries what the approval dialog has to show");

    Check(loop.TakePairingRequests(2).empty(), "after that the queue really is empty");

    loop.PushPairingRequest(Request(9));
    const std::vector<PairingRequest> all = loop.TakePairingRequests();
    Check(all.size() == 1 && all[0].addrPacked == 9,
        "with no window given, everything queued is taken at once");
    Check(loop.TakePairingRequests(0).empty(), "a zero window takes nothing and drops nothing");
}

}

void RunAgentLoopTests() {
    std::printf("--- session: the pairing queue the desktop UIs poll ---\n");
    TestPairingRequestsSurviveASmallWindow();
}
