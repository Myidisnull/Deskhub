#pragma once
#include "deskhubp/input/LocalInput.h"
#include "deskhubp/system/Clock.h"

namespace deskhubp {

template <class Derived>
class LocalInputGate {
public:
    void SetEnabled(bool on) {
        if (enabled_ == on) return;
        if (!on) static_cast<Derived*>(this)->ReleaseAll();
        enabled_ = on;
    }

    bool enabled() const {
        return enabled_;
    }

    void SetLocalMonitor(LocalInputMonitor* mon) {
        localMon_ = mon;
    }

    bool localUserActive() const {
        return localMon_ && localMon_->LocalActive(NowUs());
    }

private:
    bool enabled_ = true;
    LocalInputMonitor* localMon_ = nullptr;
};

}
