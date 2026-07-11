#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>

namespace streamsoft {

class OutgoingQueue {
public:
    void push(std::string text) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(text));
        cv_.notify_one();
    }

    bool pop_for(std::chrono::milliseconds timeout, std::string& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [&] { return !queue_.empty(); })) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
};

}
