#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/session/client/ConnectFlow.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

SourceInfo Info(uint8_t id) {
    SourceInfo s;
    s.sourceId = id;
    return s;
}

void TestDecision() {
    std::printf("[connect] the source-query result decides picker vs auto-start...\n");

    const std::vector<SourceInfo> none;
    const ConnectDecision empty = DecideAfterSourceQuery(none);
    Check(!empty.showPicker && empty.sourceId == 0,
        "no sources (or a failed query) -> start source 0 and let the host answer");

    const std::vector<SourceInfo> one{Info(3)};
    const ConnectDecision single = DecideAfterSourceQuery(one);
    Check(!single.showPicker && single.sourceId == 3,
        "exactly one source -> auto-start it without a picker");

    const std::vector<SourceInfo> many{Info(1), Info(2)};
    Check(DecideAfterSourceQuery(many).showPicker, "more than one source -> ask the user");
}

HostCaps Caps(bool terminal, bool files) {
    HostCaps caps;
    caps.terminal = terminal;
    caps.files = files;
    return caps;
}

void TestNothingTickedIsCaughtBeforeAnythingOpens() {
    std::printf("[connect] connecting with no box ticked opens nothing and says so...\n");
    const std::vector<SourceInfo> one{Info(3)};
    const ConnectPlan plan = PlanAfterConnect(Caps(true, true), one, OpenChoice{});
    Check(!plan.openShell && !plan.openFiles && !plan.openDesktop, "nothing is opened");
    Check(plan.problem == ConnectProblem::NothingTicked, "and the user is told why");
}

void TestShellAndFilesFollowWhatTheHostShares() {
    std::printf("[connect] a shell or a transfer only opens if the host offers it...\n");
    const std::vector<SourceInfo> none;

    const ConnectPlan both =
        PlanAfterConnect(Caps(true, true), none, OpenChoice{false, true, true});
    Check(both.openShell && both.openFiles, "a host offering both opens both");
    Check(both.problem == ConnectProblem::None, "with nothing to report");

    const ConnectPlan noShell =
        PlanAfterConnect(Caps(false, true), none, OpenChoice{false, true, false});
    Check(!noShell.openShell, "a host with no terminal opens no shell");
    Check(noShell.problem == ConnectProblem::HostHasNoTerminal, "and says the host has none");

    const ConnectPlan noFiles =
        PlanAfterConnect(Caps(true, false), none, OpenChoice{false, false, true});
    Check(!noFiles.openFiles, "a host not taking files opens no transfer");
    Check(noFiles.problem == ConnectProblem::HostNotTakingFiles, "and says it is not taking");
}

void TestOneRefusalDoesNotBlockTheOther() {
    std::printf("[connect] one thing the host refuses does not hold back the rest...\n");
    const std::vector<SourceInfo> one{Info(7)};
    const ConnectPlan plan =
        PlanAfterConnect(Caps(false, true), one, OpenChoice{true, true, true});
    Check(!plan.openShell, "the shell the host cannot give is skipped");
    Check(plan.openFiles, "the transfer still opens");
    Check(plan.openDesktop && plan.sourceId == 7, "and so does the screen it does share");
    Check(plan.problem == ConnectProblem::HostHasNoTerminal, "the refusal is still reported");
}

void TestDesktopFollowsTheSourceQuery() {
    std::printf("[connect] the desktop half reuses the picker rule...\n");
    const std::vector<SourceInfo> none;
    const ConnectPlan empty =
        PlanAfterConnect(Caps(true, true), none, OpenChoice{true, false, false});
    Check(!empty.openDesktop, "a host sharing no screen opens no viewer");
    Check(empty.problem == ConnectProblem::NoSourcesShared, "and says nothing is shared");

    const std::vector<SourceInfo> one{Info(5)};
    const ConnectPlan single =
        PlanAfterConnect(Caps(true, true), one, OpenChoice{true, false, false});
    Check(single.openDesktop && !single.showPicker && single.sourceId == 5,
        "one screen opens straight away");

    const std::vector<SourceInfo> two{Info(1), Info(2)};
    const ConnectPlan many =
        PlanAfterConnect(Caps(true, true), two, OpenChoice{true, false, false});
    Check(many.openDesktop && many.showPicker, "two screens ask which one");
}

void TestEmptySourcesOnlyMatterWhenTheDesktopWasAskedFor() {
    std::printf("[connect] a host sharing no screen is fine if nobody asked for one...\n");
    const std::vector<SourceInfo> none;
    const ConnectPlan plan =
        PlanAfterConnect(Caps(true, false), none, OpenChoice{false, true, false});
    Check(plan.openShell, "the shell opens");
    Check(plan.problem == ConnectProblem::None, "and the empty source list is not an error");
}

void TestEveryProblemHasSomethingToShow() {
    std::printf("[connect] every refusal comes with text a client can put on screen...\n");
    Check(ConnectProblemText(ConnectProblem::None, "10.0.0.4").empty(),
        "no problem means no message");
    for (const ConnectProblem problem :
        {ConnectProblem::NothingTicked, ConnectProblem::HostHasNoTerminal,
            ConnectProblem::HostNotTakingFiles, ConnectProblem::NoSourcesShared})
        Check(!ConnectProblemText(problem, "10.0.0.4").empty(), "every refusal has text");
    Check(ConnectProblemText(ConnectProblem::NoSourcesShared, "10.0.0.4").find("10.0.0.4") !=
              std::string::npos,
        "and the one about a specific host names it");
}

}

void RunConnectFlowTests() {
    TestDecision();
    TestNothingTickedIsCaughtBeforeAnythingOpens();
    TestShellAndFilesFollowWhatTheHostShares();
    TestOneRefusalDoesNotBlockTheOther();
    TestDesktopFollowsTheSourceQuery();
    TestEmptySourcesOnlyMatterWhenTheDesktopWasAskedFor();
    TestEveryProblemHasSomethingToShow();
}
