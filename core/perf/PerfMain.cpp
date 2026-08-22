#include "Perf.h"
#include "PerfHarness.h"

int main() {
    deskhub::perf::Begin("core performance suite (offline: no network, no GPU)",
        "out/perf/baseline.txt");

    RunVideoPerf();
    RunTransferPerf();
    RunTerminalPerf();
    RunControlPerf();
    RunAudioPerf();
    RunInputPerf();
    RunStreamPerf();

    return deskhub::perf::Summary();
}
