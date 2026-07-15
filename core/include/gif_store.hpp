#pragma once

#include <crow/json.h>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace streamsoft {

struct GifEntry {
    std::string name;
    int price = 50;
};

class GifStore {
public:
    static constexpr const char* kFile = "gifs.json";

    GifStore() { load(); }

    void load() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();

        std::ifstream f(kFile, std::ios::binary);
        if (!f) return;

        std::ostringstream ss;
        ss << f.rdbuf();
        auto arr = crow::json::load(ss.str());
        if (!arr) return;

        for (const auto& item : arr) {
            GifEntry e;
            e.name = item.has("name") ? std::string(item["name"].s()) : "";
            e.price = item.has("price") ? static_cast<int>(item["price"].i()) : 50;
            if (!e.name.empty()) entries_.push_back(std::move(e));
        }
    }

    void save_unlocked() const {
        std::vector<crow::json::wvalue> arr;
        for (const auto& e : entries_) {
            crow::json::wvalue j;
            j["name"] = e.name;
            j["price"] = e.price;
            arr.push_back(std::move(j));
        }
        crow::json::wvalue root(std::move(arr));

        std::ofstream f(kFile, std::ios::binary | std::ios::trunc);
        f << root.dump();
    }

    std::vector<GifEntry> all() {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_;
    }

    crow::json::wvalue list() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<crow::json::wvalue> arr;
        for (const auto& e : entries_) {
            crow::json::wvalue j;
            j["name"] = e.name;
            j["price"] = e.price;
            arr.push_back(std::move(j));
        }
        return crow::json::wvalue(std::move(arr));
    }

    void upsert(std::string name, int price) {
        name = normalize(name);
        if (name.empty()) return;

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& e : entries_) {
            if (e.name == name) {
                e.price = std::max(0, price);
                save_unlocked();
                return;
            }
        }
        GifEntry e;
        e.name = name;
        e.price = std::max(0, price);
        entries_.push_back(std::move(e));
        save_unlocked();
    }

    void remove(std::string name) {
        name = normalize(name);
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                       [&](const GifEntry& e) { return e.name == name; }),
                        entries_.end());
        save_unlocked();
    }

    std::optional<GifEntry> find(std::string name) {
        name = normalize(name);
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& e : entries_) {
            if (e.name == name) return e;
        }
        return std::nullopt;
    }

    static std::string normalize(std::string name) {
        name = trim(name);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        return name;
    }

private:
    static std::string trim(std::string s) {
        auto not_space = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    }

    std::mutex mutex_;
    std::vector<GifEntry> entries_;
};

}
