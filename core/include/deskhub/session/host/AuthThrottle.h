#pragma once
#include "deskhub/session/host/ScreenHostSession.h"

#include <cstdint>

namespace deskhub {

class AuthThrottle {
public:
    bool Locked(uint64_t nowUs) const {
        return nowUs < lockUntilUs_;
    }

    void RecordFailure(uint64_t nowUs) {
        if (++wrongAttempts_ < kMaxPasscodeAttempts) return;
        wrongAttempts_ = 0;
        lockUntilUs_ = nowUs + kPasscodeLockoutUs;
    }

    void RecordSuccess() {
        wrongAttempts_ = 0;
    }

private:
    uint32_t wrongAttempts_ = 0;
    uint64_t lockUntilUs_ = 0;
};

}
