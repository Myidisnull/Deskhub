#include "Perf.h"
#include "PerfHarness.h"

int main() {
    deskhub::perf::Begin();

    RunVideoPerf();
    RunTransferPerf();
    RunTerminalPerf();
    RunControlPerf();
    RunAudioPerf();
    RunInputPerf();
    RunStreamPerf();

    return deskhub::perf::Summary();
}
