#include "Perf.h"
#include "PerfHarness.h"

int main() {
    deskhub::perf::Begin();

    RunVideoPerf();
    RunTransferPerf();
    RunTerminalPerf();
    RunControlPerf();

    return deskhub::perf::Summary();
}
