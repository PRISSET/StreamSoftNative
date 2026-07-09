#pragma once

// Thread-safe blocking queue for outgoing chat messages — mirrors the role
// of the asyncio.Queue used for `twitch_outgoing` in softforstream/main.py,
// just backed by a std::mutex/condition_variable instead of an event loop.
//
// Lives for the whole process (owned by main), independent of any single
// IRC connection's lifetime — a reconnect just gets a fresh writer thread
// that resumes draining whatever's still queued.

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

    // Waits up to `timeout` for an item. Returns false on timeout (caller
    // should re-check its own "still connected" condition and call again).
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

} // namespace streamsoft
