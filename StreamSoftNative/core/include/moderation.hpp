#pragma once

// Mirrors softforstream/moderation.py.

#include <algorithm>
#include <cctype>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace streamsoft {

class ModerationState {
public:
    void mute(const std::string& username) {
        std::lock_guard<std::mutex> lock(mutex_);
        muted_.insert(normalize(username));
    }

    void unmute(const std::string& username) {
        std::lock_guard<std::mutex> lock(mutex_);
        muted_.erase(normalize(username));
    }

    bool is_muted(const std::string& username) {
        std::lock_guard<std::mutex> lock(mutex_);
        return muted_.count(normalize(username)) > 0;
    }

    std::vector<std::string> list_muted() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<std::string>(muted_.begin(), muted_.end()); // std::set is already sorted
    }

private:
    static std::string normalize(std::string username) {
        // trim
        auto not_space = [](unsigned char c) { return !std::isspace(c); };
        username.erase(username.begin(), std::find_if(username.begin(), username.end(), not_space));
        username.erase(std::find_if(username.rbegin(), username.rend(), not_space).base(), username.end());

        while (!username.empty() && username.front() == '@') username.erase(username.begin());

        std::transform(username.begin(), username.end(), username.begin(), ::tolower);
        return username;
    }

    std::mutex mutex_;
    std::set<std::string> muted_;
};

} // namespace streamsoft
