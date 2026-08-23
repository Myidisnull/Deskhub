#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace deskhubp {

inline constexpr uint32_t kDeviceStatusRoundSecs = 30;

struct DeviceStatus {
    std::string addr;
    bool online = false;
    uint32_t rttMs = 0;
};

class DeviceStatusPoller {
public:
    using StatusHandler = std::function<void(const DeviceStatus&)>;

    DeviceStatusPoller() = default;
    ~DeviceStatusPoller();
    DeviceStatusPoller(const DeviceStatusPoller&) = delete;
    DeviceStatusPoller& operator=(const DeviceStatusPoller&) = delete;

    void Start(StatusHandler onStatus);
    void SetAddresses(std::vector<std::string> addrs);
    void RefreshNow();
    void Stop();

private:
    void ThreadMain();
    std::vector<std::string> SnapshotAddresses();
    bool ShouldStop();
    bool WaitForNextRound();

    StatusHandler onStatus_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::string> addrs_;
    bool stop_ = false;
    bool dirty_ = false;
};

}
