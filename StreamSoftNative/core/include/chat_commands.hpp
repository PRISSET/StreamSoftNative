#pragma once

// Mirrors softforstream/chat_commands.py — same JSON file format
// (chat_commands.json), same trigger normalization (lowercase, "!" prefix),
// same cooldown behavior.

#include <crow/json.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace streamsoft {

struct ChatCommand {
    std::string trigger;
    std::string response;
    bool enabled = true;
    int cooldown = 15;
};

class CommandsStore {
public:
    static constexpr const char* kFile = "chat_commands.json";

    CommandsStore() { load(); }

    void load() {
        std::lock_guard<std::mutex> lock(mutex_);
        commands_.clear();

        std::ifstream f(kFile, std::ios::binary);
        if (!f) return;

        std::ostringstream ss;
        ss << f.rdbuf();
        auto arr = crow::json::load(ss.str());
        if (!arr) return;

        for (const auto& item : arr) {
            ChatCommand c;
            c.trigger = item.has("trigger") ? std::string(item["trigger"].s()) : "";
            c.response = item.has("response") ? std::string(item["response"].s()) : "";
            c.enabled = item.has("enabled") ? item["enabled"].b() : true;
            c.cooldown = item.has("cooldown") ? static_cast<int>(item["cooldown"].i()) : 15;
            if (!c.trigger.empty()) commands_.push_back(std::move(c));
        }
    }

    void save_unlocked() const {
        std::vector<crow::json::wvalue> arr;
        for (const auto& c : commands_) {
            crow::json::wvalue j;
            j["trigger"] = c.trigger;
            j["response"] = c.response;
            j["enabled"] = c.enabled;
            j["cooldown"] = c.cooldown;
            arr.push_back(std::move(j));
        }
        crow::json::wvalue root(std::move(arr));

        std::ofstream f(kFile, std::ios::binary | std::ios::trunc);
        f << root.dump();
    }

    crow::json::wvalue list() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<crow::json::wvalue> arr;
        for (const auto& c : commands_) {
            crow::json::wvalue j;
            j["trigger"] = c.trigger;
            j["response"] = c.response;
            j["enabled"] = c.enabled;
            j["cooldown"] = c.cooldown;
            arr.push_back(std::move(j));
        }
        return crow::json::wvalue(std::move(arr));
    }

    void add(std::string trigger, const std::string& response, int cooldown) {
        trigger = normalize(trigger);
        if (trigger.empty() || response.empty()) return;

        std::lock_guard<std::mutex> lock(mutex_);
        commands_.erase(std::remove_if(commands_.begin(), commands_.end(),
                                        [&](const ChatCommand& c) { return c.trigger == trigger; }),
                         commands_.end());
        ChatCommand c;
        c.trigger = trigger;
        c.response = response;
        c.cooldown = std::max(0, cooldown);
        commands_.push_back(std::move(c));
        save_unlocked();
    }

    void remove(std::string trigger) {
        trigger = normalize(trigger);
        std::lock_guard<std::mutex> lock(mutex_);
        commands_.erase(std::remove_if(commands_.begin(), commands_.end(),
                                        [&](const ChatCommand& c) { return c.trigger == trigger; }),
                         commands_.end());
        last_used_.erase(trigger);
        save_unlocked();
    }

    void set_enabled(std::string trigger, bool enabled) {
        trigger = normalize(trigger);
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& c : commands_) {
            if (c.trigger == trigger) c.enabled = enabled;
        }
        save_unlocked();
    }

    // Returns the response text if `text` starts with a known, enabled,
    // off-cooldown trigger — same first-match-wins semantics as the Python
    // reference.
    std::optional<std::string> match(const std::string& text) {
        std::string stripped = trim(text);
        if (stripped.empty()) return std::nullopt;

        std::string first_word = stripped.substr(0, stripped.find_first_of(" \t"));
        std::transform(first_word.begin(), first_word.end(), first_word.begin(), ::tolower);

        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& c : commands_) {
            if (!c.enabled || c.trigger != first_word) continue;

            auto now = std::chrono::steady_clock::now();
            auto it = last_used_.find(c.trigger);
            if (it != last_used_.end()) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                if (elapsed < c.cooldown) return std::nullopt;
            }
            last_used_[c.trigger] = now;
            return c.response;
        }
        return std::nullopt;
    }

private:
    static std::string trim(std::string s) {
        auto not_space = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    }

    static std::string normalize(std::string trigger) {
        trigger = trim(trigger);
        std::transform(trigger.begin(), trigger.end(), trigger.begin(), ::tolower);
        if (!trigger.empty() && trigger.front() != '!') trigger = "!" + trigger;
        return trigger;
    }

    std::mutex mutex_;
    std::vector<ChatCommand> commands_;
    std::map<std::string, std::chrono::steady_clock::time_point> last_used_;
};

} // namespace streamsoft
