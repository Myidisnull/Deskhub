#pragma once

#include <cstdint>
#include <vector>

namespace deskhub {

inline constexpr uint32_t kMaxAuthFailures = 5;
inline constexpr uint64_t kAuthWindowUs = 60'000'000;
inline constexpr uint64_t kAuthLockoutUs = 30'000'000;
inline constexpr size_t kMaxAuthRateSlots = 64;

class AuthRateLimit {
public:
    bool Locked(uint64_t fromPacked, uint64_t nowUs) const {
        if (const Slot* s = Find(fromPacked)) return nowUs < s->lockUntilUs;
        return false;
    }

    void NoteSuccess(uint64_t fromPacked) {
        if (Slot* s = FindMutable(fromPacked)) {
            s->fails = 0;
            s->windowStartUs = 0;
            s->lockUntilUs = 0;
        }
    }

    void NoteFailure(uint64_t fromPacked, uint64_t nowUs) {
        Slot& s = Ensure(fromPacked, nowUs);
        if (nowUs < s.lockUntilUs) return;
        if (s.windowStartUs == 0 || nowUs - s.windowStartUs > kAuthWindowUs) {
            s.windowStartUs = nowUs;
            s.fails = 0;
        }
        if (++s.fails >= kMaxAuthFailures) {
            s.fails = 0;
            s.windowStartUs = 0;
            s.lockUntilUs = nowUs + kAuthLockoutUs;
        }
    }

private:
    struct Slot {
        uint64_t fromPacked = 0;
        uint32_t fails = 0;
        uint64_t windowStartUs = 0;
        uint64_t lockUntilUs = 0;
        bool used = false;
    };

    const Slot* Find(uint64_t fromPacked) const {
        for (const Slot& s : slots_)
            if (s.used && s.fromPacked == fromPacked) return &s;
        return nullptr;
    }

    Slot* FindMutable(uint64_t fromPacked) {
        for (Slot& s : slots_)
            if (s.used && s.fromPacked == fromPacked) return &s;
        return nullptr;
    }

    Slot& Ensure(uint64_t fromPacked, uint64_t nowUs) {
        if (Slot* s = FindMutable(fromPacked)) return *s;
        for (Slot& s : slots_) {
            if (!s.used) {
                s = Slot{fromPacked, 0, 0, 0, true};
                return s;
            }
        }
        size_t victim = 0;
        uint64_t oldest = ~uint64_t{0};
        for (size_t i = 0; i < kMaxAuthRateSlots; ++i) {
            const uint64_t t = slots_[i].lockUntilUs ? slots_[i].lockUntilUs
                                                     : slots_[i].windowStartUs;
            if (t < oldest) {
                oldest = t;
                victim = i;
            }
        }
        (void)nowUs;
        slots_[victim] = Slot{fromPacked, 0, 0, 0, true};
        return slots_[victim];
    }

    Slot slots_[kMaxAuthRateSlots]{};
};

}
