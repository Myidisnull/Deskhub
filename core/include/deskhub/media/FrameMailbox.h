#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

namespace deskhub::media {

template <class Frame>
class FrameMailbox {
public:
    std::optional<Frame> Put(Frame&& frame) {
        std::optional<Frame> displaced;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (closed_) return std::optional<Frame>(std::move(frame));
            if (pending_) displaced = std::move(pending_);
            pending_ = std::move(frame);
        }
        cv_.notify_one();
        return displaced;
    }

    bool TakeWait(Frame& out) {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return pending_.has_value() || closed_; });
        if (!pending_) return false;
        out = std::move(*pending_);
        pending_.reset();
        return true;
    }

    void Close() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            closed_ = true;
            pending_.reset();
        }
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<Frame> pending_;
    bool closed_ = false;
};

}
