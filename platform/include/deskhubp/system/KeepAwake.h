#pragma once

#include <atomic>
#include <mutex>

namespace deskhubp {

void SetKeepAwakeActive(bool on);

inline std::atomic<int>& KeepAwakeCount() {
    static std::atomic<int> count{0};
    return count;
}

inline std::mutex& KeepAwakeMutex() {
    static std::mutex mutex;
    return mutex;
}

inline void AcquireKeepAwake() {
    const std::lock_guard<std::mutex> lock(KeepAwakeMutex());
    if (KeepAwakeCount().fetch_add(1) == 0) SetKeepAwakeActive(true);
}

inline void ReleaseKeepAwake() {
    const std::lock_guard<std::mutex> lock(KeepAwakeMutex());
    if (KeepAwakeCount().fetch_sub(1) == 1) SetKeepAwakeActive(false);
}

}
