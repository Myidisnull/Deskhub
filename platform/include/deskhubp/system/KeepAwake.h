#pragma once

#include <atomic>

namespace deskhubp {

void SetKeepAwakeActive(bool on);

inline std::atomic<int>& KeepAwakeCount() {
    static std::atomic<int> count{0};
    return count;
}

inline void AcquireKeepAwake() {
    if (KeepAwakeCount().fetch_add(1) == 0) SetKeepAwakeActive(true);
}

inline void ReleaseKeepAwake() {
    if (KeepAwakeCount().fetch_sub(1) == 1) SetKeepAwakeActive(false);
}

}
