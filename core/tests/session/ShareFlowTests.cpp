#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/session/ShareFlow.h"

#include <cstdio>
#include <vector>

using namespace deskhub;

namespace {

void TestClampKeepsTheFirstSources() {
    std::printf("[share] more displays than the protocol carries get clamped, with a note...\n");
    std::vector<media::ShareSource> many;
    for (uint64_t i = 0; i < kMaxSources + 3; ++i) {
        media::ShareSource s;
        s.targetId = i + 1;
        many.push_back(s);
    }

    const ShareClampResult clamped = ClampShareSources(many);
    Check(clamped.clamped, "the caller is told to warn the user");
    Check(clamped.sources.size() == kMaxSources, "only the protocol maximum survives");
    Check(clamped.sources.front().targetId == 1 && clamped.sources.back().targetId == kMaxSources,
        "and it is the first N displays, in order");

    const ShareClampResult fits = ClampShareSources({many.begin(), many.begin() + 2});
    Check(!fits.clamped && fits.sources.size() == 2, "a small list passes through untouched");
}

}

void RunShareFlowTests() {
    TestClampKeepsTheFirstSources();
}
