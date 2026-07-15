#pragma once

#include <crow/json.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace streamsoft {

class PointsStore {
public:
    static constexpr const char* kFile = "points.json";
    static constexpr int kAwardCooldownSeconds = 30;

    PointsStore() { load(); }

    void load() {
        std::lock_guard<std::mutex> lock(mutex_);
        balances_.clear();

        std::ifstream f(kFile, std::ios::binary);
        if (!f) return;

        std::ostringstream ss;
        ss << f.rdbuf();
        auto j = crow::json::load(ss.str());
        if (!j) return;

        for (const auto& key : j.keys()) {
            balances_[key] = static_cast<int>(j[key].i());
        }
    }

    void save_unlocked() const {
        crow::json::wvalue j;
        for (const auto& [name, points] : balances_) j[name] = points;
        std::ofstream f(kFile, std::ios::binary | std::ios::trunc);
        f << j.dump();
    }

    void award_for_message(const std::string& username, int amount) {
        if (amount <= 0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto it = last_award_.find(username);
        if (it != last_award_.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
            if (elapsed < kAwardCooldownSeconds) return;
        }
        last_award_[username] = now;
        balances_[username] += amount;
        save_unlocked();
    }

    void add(const std::string& username, int amount) {
        if (amount == 0) return;
        std::lock_guard<std::mutex> lock(mutex_);
        balances_[username] += amount;
        save_unlocked();
    }

    int balance(const std::string& username) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = balances_.find(username);
        return it != balances_.end() ? it->second : 0;
    }

    bool spend(const std::string& username, int amount) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = balances_.find(username);
        int have = it != balances_.end() ? it->second : 0;
        if (have < amount) return false;
        balances_[username] = have - amount;
        save_unlocked();
        return true;
    }

    crow::json::wvalue leaderboard(size_t limit = 20) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<std::string, int>> sorted(balances_.begin(), balances_.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (sorted.size() > limit) sorted.resize(limit);

        std::vector<crow::json::wvalue> arr;
        for (const auto& [name, points] : sorted) {
            crow::json::wvalue j;
            j["username"] = name;
            j["points"] = points;
            arr.push_back(std::move(j));
        }
        return crow::json::wvalue(std::move(arr));
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, int> balances_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_award_;
};

}
