#include "deskhubp/net/DeviceStatusPoller.h"

#include <chrono>
#include <utility>

#include "deskhubp/net/HostProbe.h"
#include "deskhubp/net/UdpSocket.h"

namespace deskhubp {

namespace {
constexpr uint32_t kProbeTimeoutMs = 900;
constexpr auto kRoundGap = std::chrono::seconds(kDeviceStatusRoundSecs);
}

DeviceStatusPoller::~DeviceStatusPoller() {
    Stop();
}

void DeviceStatusPoller::Start(StatusHandler onStatus) {
    Stop();
    {
        std::lock_guard lock(mutex_);
        stop_ = false;
        dirty_ = true;
    }
    onStatus_ = std::move(onStatus);
    thread_ = std::thread(&DeviceStatusPoller::ThreadMain, this);
}

void DeviceStatusPoller::SetAddresses(std::vector<std::string> addrs) {
    std::lock_guard lock(mutex_);
    if (addrs == addrs_) return;
    addrs_ = std::move(addrs);
    dirty_ = true;
    wake_.notify_all();
}

void DeviceStatusPoller::RefreshNow() {
    std::lock_guard lock(mutex_);
    dirty_ = true;
    wake_.notify_all();
}

void DeviceStatusPoller::Stop() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        wake_.notify_all();
    }
    if (thread_.joinable()) thread_.join();
}

std::vector<std::string> DeviceStatusPoller::SnapshotAddresses() {
    std::lock_guard lock(mutex_);
    dirty_ = false;
    return addrs_;
}

bool DeviceStatusPoller::ShouldStop() {
    std::lock_guard lock(mutex_);
    return stop_;
}

bool DeviceStatusPoller::WaitForNextRound() {
    std::unique_lock lock(mutex_);
    wake_.wait_for(lock, kRoundGap, [this] { return stop_ || dirty_; });
    return !stop_;
}

void DeviceStatusPoller::ThreadMain() {
    for (;;) {
        const std::vector<std::string> addrs = SnapshotAddresses();

        std::vector<NetAddr> nets;
        std::vector<size_t> netIndex;
        nets.reserve(addrs.size());
        for (size_t i = 0; i < addrs.size(); ++i) {
            NetAddr parsed{};
            if (!ParseNetAddr(addrs[i], parsed)) continue;
            nets.push_back(parsed);
            netIndex.push_back(i);
        }

        const auto rtts = ProbeHostsRttMs(nets, kProbeTimeoutMs);
        if (ShouldStop()) return;

        if (onStatus_) {
            std::vector<bool> reported(addrs.size(), false);
            for (size_t k = 0; k < nets.size(); ++k) {
                DeviceStatus status;
                status.addr = addrs[netIndex[k]];
                status.online = rtts[k].has_value();
                status.rttMs = rtts[k].value_or(0);
                reported[netIndex[k]] = true;
                onStatus_(status);
            }
            for (size_t i = 0; i < addrs.size(); ++i)
                if (!reported[i]) onStatus_(DeviceStatus{addrs[i], false, 0});
        }

        if (!WaitForNextRound()) return;
    }
}

}
