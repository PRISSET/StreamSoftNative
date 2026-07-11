#pragma once

#include <crow/json.h>

#include <deque>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace streamsoft {

struct SongRequest {
    std::string platform;
    std::string ref;
    std::string requester;
};

struct ParsedSongLink {
    bool valid = false;
    std::string platform;
    std::string ref;
};

inline ParsedSongLink parse_song_link(const std::string& text) {
    static const std::regex yt_re(R"((?:youtube\.com/watch\?v=|youtu\.be/)([A-Za-z0-9_-]{6,}))");
    static const std::regex sc_re(R"((https?://(?:www\.)?soundcloud\.com/\S+))");

    std::smatch m;
    if (std::regex_search(text, m, yt_re)) return {true, "youtube", m[1].str()};
    if (std::regex_search(text, m, sc_re)) return {true, "soundcloud", m[1].str()};
    return {};
}

class SongQueue {
public:
    void enqueue(SongRequest req) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(req));
    }

    bool has_current() {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_.has_value();
    }

    std::optional<SongRequest> advance() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            current_.reset();
            return std::nullopt;
        }
        current_ = std::move(queue_.front());
        queue_.pop_front();
        return current_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        current_.reset();
    }

    crow::json::wvalue status() {
        std::lock_guard<std::mutex> lock(mutex_);
        crow::json::wvalue j;
        if (current_) {
            j["current"]["platform"] = current_->platform;
            j["current"]["ref"] = current_->ref;
            j["current"]["requester"] = current_->requester;
        } else {
            j["current"] = nullptr;
        }
        std::vector<crow::json::wvalue> arr;
        for (const auto& r : queue_) {
            crow::json::wvalue item;
            item["platform"] = r.platform;
            item["ref"] = r.ref;
            item["requester"] = r.requester;
            arr.push_back(std::move(item));
        }
        j["queue"] = std::move(arr);
        return j;
    }

private:
    std::mutex mutex_;
    std::deque<SongRequest> queue_;
    std::optional<SongRequest> current_;
};

}
