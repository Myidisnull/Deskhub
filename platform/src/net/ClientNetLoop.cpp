#include "deskhubp/net/ClientNetLoop.h"

#include "deskhubp/system/Clock.h"

#ifdef _WIN32
#include <processthreadsapi.h>
#else
#include <unistd.h>
#endif

namespace deskhubp {

namespace {

uint32_t CurrentProcessId() {
#ifdef _WIN32
    return uint32_t(GetCurrentProcessId());
#else
    return uint32_t(getpid());
#endif
}

}

uint32_t MakeClientId(uint8_t sourceId) {
    return uint32_t(NowUs()) ^ CurrentProcessId() ^ (uint32_t(sourceId) << 24);
}

void RunClientNetLoop(SessionTransport& sock, deskhub::ClientPump& pump,
    const ClientNetLoopHooks& hooks) {
    uint8_t buf[deskhub::kMaxDatagram];

    for (;;) {
        if (hooks.stopped && hooks.stopped()) break;

        NetAddr from;
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        const uint64_t now = NowUs();
        if (n < 0) {
            if (hooks.onSocketError) hooks.onSocketError();
            break;
        }
        if (n > 0) pump.OnDatagram(std::span<const uint8_t>(buf, size_t(n)), now);

        pump.PollFrames(now);
        if (hooks.afterFrames) hooks.afterFrames(pump, now);

        pump.PlanNacks(now);
        if (hooks.beforeTick) hooks.beforeTick(pump, now);

        if (!pump.Tick(now)) break;
        if (hooks.onPhase) hooks.onPhase(pump.streaming());

        pump.CountLoopBusy(now, NowUs());
    }

    pump.SendBye();
}

}
