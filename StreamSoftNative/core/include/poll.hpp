#pragma once

// In-chat polls: viewers vote by typing !1 / !2 / !3 ... while a poll is
// active, one vote per username (voting again just moves your existing
// vote, doesn't double-count) — same mutex-guarded-state shape as
// CommandsStore's match(), just in-memory only since a poll is inherently
// a single-session, not-worth-persisting-to-disk thing.

#include <crow/json.h>

#include <cctype>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace streamsoft {

class PollState {
public:
    void start(const std::string& question, const std::vector<std::string>& options) {
        std::lock_guard<std::mutex> lock(mutex_);
        question_ = question;
        options_ = options;
        votes_.assign(options.size(), 0);
        voters_.clear();
        active_ = true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
    }

    bool active() {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

    // Returns true if `text` was consumed as a vote — callers use this to
    // skip the normal chat broadcast/TTS-read path for it (a chat flooded
    // with bare "!1"/"!2" isn't meant to be displayed or read aloud one by
    // one). Only a bare "!<digit>" counts, "!1abc" or "!12" don't — keeps
    // this from colliding with actual chat commands that happen to start
    // the same way.
    bool try_vote(const std::string& username, const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) return false;
        if (text.size() != 2 || text[0] != '!') return false;
        if (!std::isdigit(static_cast<unsigned char>(text[1]))) return false;

        int choice = text[1] - '0' - 1; // "!1" -> option index 0
        if (choice < 0 || choice >= static_cast<int>(options_.size())) return false;

        auto it = voters_.find(username);
        if (it != voters_.end()) {
            if (it->second == choice) return true; // same vote again, no-op
            votes_[it->second]--;
        }
        votes_[choice]++;
        voters_[username] = choice;
        return true;
    }

    crow::json::wvalue status() {
        std::lock_guard<std::mutex> lock(mutex_);
        crow::json::wvalue j;
        j["active"] = active_;
        j["question"] = question_;
        std::vector<crow::json::wvalue> opts;
        for (size_t i = 0; i < options_.size(); ++i) {
            crow::json::wvalue o;
            o["text"] = options_[i];
            o["votes"] = i < votes_.size() ? votes_[i] : 0;
            opts.push_back(std::move(o));
        }
        j["options"] = std::move(opts);
        j["total_votes"] = static_cast<int>(voters_.size());
        return j;
    }

private:
    std::mutex mutex_;
    bool active_ = false;
    std::string question_;
    std::vector<std::string> options_;
    std::vector<int> votes_;
    std::unordered_map<std::string, int> voters_;
};

}  // namespace streamsoft
