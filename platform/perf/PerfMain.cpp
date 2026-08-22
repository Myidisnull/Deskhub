#include "PerfHarness.h"

void RunQuicPerf();

int main() {
    deskhub::perf::Begin("platform performance suite (loopback sockets, real QUIC)",
        "out/perf/platform-baseline.txt");

    RunQuicPerf();

    return deskhub::perf::Summary();
}
