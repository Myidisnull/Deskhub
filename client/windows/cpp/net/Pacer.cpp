#include "net/Pacer.h"

#include "deskhubp/Clock.h"

namespace {

constexpr uint64_t kMinSleepUs = 500;

}

Pacer::~Pacer() {
    if (timer_) CloseHandle(timer_);
}

void Pacer::SleepUs(uint64_t us) {
    if (!timer_) {
        timer_ = CreateWaitableTimerExW(nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS);
    }
    if (timer_) {
        LARGE_INTEGER due;
        due.QuadPart = -int64_t(us) * 10;
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer_, INFINITE);
            return;
        }
    }
    Sleep(DWORD((us + 999) / 1000));
}

void Pacer::Gate(size_t bytes) {
    if (!rateBps_ || !bytes) return;

    const uint64_t now = NowUs();

    if (nextUs_ < now) nextUs_ = now;

    if (const uint64_t waitUs = nextUs_ - now; waitUs >= kMinSleepUs) SleepUs(waitUs);

    nextUs_ += uint64_t(bytes) * 8 * 1'000'000ull / rateBps_;
}
