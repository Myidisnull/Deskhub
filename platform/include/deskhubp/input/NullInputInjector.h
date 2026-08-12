#pragma once
#include <cstdint>

#include "deskhub/protocol/Wire.h"

namespace deskhubp {

class NullInputInjector {
public:
    void Apply(const deskhub::InputEvent&) {
        ++skipped_;
    }

    void ReleaseAll() {}

    uint64_t skipped() const {
        return skipped_;
    }

private:
    uint64_t skipped_ = 0;
};

}
