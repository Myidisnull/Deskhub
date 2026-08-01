#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/session/OpenViewers.h"

#include <cstdio>

using namespace deskhub;

namespace {

void TestTheLastCloseIsTheOneThatReports() {
    std::printf("[viewers] only the close that empties the app reports it...\n");
    OpenViewerCount v;
    Check(v.none(), "an app with no viewer open is empty");

    v.Opened();
    v.Opened();
    Check(v.count() == 2, "two viewers are open");
    Check(!v.Closed(), "closing the first leaves one behind");
    Check(v.Closed(), "closing the second empties the app");
    Check(v.none(), "and it stays empty");
}

void TestASingleViewerRoundTrips() {
    std::printf("[viewers] the common case: one viewer opens and closes...\n");
    OpenViewerCount v;
    v.Opened();
    Check(!v.none(), "a viewer is open");
    Check(v.Closed(), "closing it is the last close");
}

void TestAnUnbalancedCloseCannotGoNegative() {
    std::printf("[viewers] a close with nothing open cannot strand the count below zero...\n");
    OpenViewerCount v;
    Check(v.Closed(), "closing nothing still reads as empty");
    Check(v.count() == 0, "and the count stays at zero rather than going negative");

    v.Opened();
    Check(v.count() == 1, "so the next open is the only one, not the second");
    Check(v.Closed(), "and closing it empties the app on schedule");
}

}

void RunOpenViewersTests() {
    TestTheLastCloseIsTheOneThatReports();
    TestASingleViewerRoundTrips();
    TestAnUnbalancedCloseCannotGoNegative();
}
