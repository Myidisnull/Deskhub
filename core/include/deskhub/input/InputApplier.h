#pragma once
#include "deskhub/input/PressedInputTracker.h"
#include "deskhub/protocol/Wire.h"

#include <cstdint>

namespace deskhub {

template <class Backend, class NativeKey>
class InputApplier {
public:
    uint64_t applied() const {
        return held_.applied();
    }
    uint64_t skipped() const {
        return held_.skipped();
    }

protected:
    Backend& backend() {
        return static_cast<Backend&>(*this);
    }

    bool DispatchInput(const InputEvent& e, bool localActive) {
        const InputGate gate = held_.Gate(localActive);
        if (!gate.allow) {
            if (gate.justSuppressed) {
                backend().OnLocalUserTookOver();
                backend().ReleaseAll();
            }
            return false;
        }
        if (gate.justResumed) backend().OnLocalUserIdle();

        switch (e.type) {
            case InputType::Key:
                backend().SendKey(e.a, e.b, e.state != 0);
                break;
            case InputType::MouseMove:
                if (e.absolute)
                    backend().SendMoveAbsolute(e.a, e.b);
                else
                    backend().SendMoveRelative(e.a, e.b);
                break;
            case InputType::MouseButton:
                backend().SendButton(MouseButton(e.a), e.state != 0);
                break;
            case InputType::MouseWheel:
                backend().SendWheel(e.b);
                break;
            default:
                return false;
        }

        held_.CountApplied();
        return true;
    }

    PressedInputTracker<NativeKey> held_;
};

}
