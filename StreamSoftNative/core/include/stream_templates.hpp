#pragma once

#include <crow/json.h>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// Named title/category presets ("Играем в CS2", "Играем в доту") the user
// picks from instead of retyping the stream title (and, for Twitch, the
// game/category) by hand before every stream — see overlay_server.hpp's
// setup_stream_template_routes() for what actually pushes a template out to
// each connected platform.
namespace streamsoft {

struct StreamTemplate {
    std::string id;
    std::string name;
    std::string title;
    std::string twitch_game;
};

// Random ASCII-only hex id — used as the REST path segment for delete/apply
// instead of the template's (usually Cyrillic) display name. Crow's <string>
// route parameter doesn't correctly percent-decode multi-byte UTF-8 path
// segments (verified live: a Cyrillic name 404'd every time, an ASCII one
// worked immediately), so routing by name at all was a dead end regardless
// of any escaping done on the JS side.
inline std::string generate_template_id() {
    static std::mt19937_64 gen(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    const char* hex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 12; ++i) s += hex[dist(gen)];
    return s;
}

class StreamTemplateStore {
public:
    static constexpr const char* kFile = "stream_templates.json";

    StreamTemplateStore() { load(); }

    void load() {
        std::lock_guard<std::mutex> lock(mutex_);
        templates_.clear();

        std::ifstream f(kFile, std::ios::binary);
        if (!f) return;

        std::ostringstream ss;
        ss << f.rdbuf();
        auto arr = crow::json::load(ss.str());
        if (!arr) return;

        for (const auto& item : arr) {
            StreamTemplate t;
            t.id = item.has("id") ? std::string(item["id"].s()) : generate_template_id();
            t.name = item.has("name") ? std::string(item["name"].s()) : "";
            t.title = item.has("title") ? std::string(item["title"].s()) : "";
            t.twitch_game = item.has("twitch_game") ? std::string(item["twitch_game"].s()) : "";
            if (!t.name.empty()) templates_.push_back(std::move(t));
        }
    }

    void save_unlocked() const {
        std::vector<crow::json::wvalue> arr;
        for (const auto& t : templates_) {
            crow::json::wvalue j;
            j["id"] = t.id;
            j["name"] = t.name;
            j["title"] = t.title;
            j["twitch_game"] = t.twitch_game;
            arr.push_back(std::move(j));
        }
        crow::json::wvalue root(std::move(arr));

        std::ofstream f(kFile, std::ios::binary | std::ios::trunc);
        f << root.dump();
    }

    crow::json::wvalue list() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<crow::json::wvalue> arr;
        for (const auto& t : templates_) {
            crow::json::wvalue j;
            j["id"] = t.id;
            j["name"] = t.name;
            j["title"] = t.title;
            j["twitch_game"] = t.twitch_game;
            arr.push_back(std::move(j));
        }
        return crow::json::wvalue(std::move(arr));
    }

    // Always creates a new entry with a fresh id — there's no in-place edit
    // in the UI yet (delete + re-add covers it), so no name-based upsert
    // logic is needed here.
    std::string add(const std::string& name, const std::string& title, const std::string& twitch_game) {
        if (name.empty()) return "";

        std::lock_guard<std::mutex> lock(mutex_);
        std::string id = generate_template_id();
        templates_.push_back(StreamTemplate{id, name, title, twitch_game});
        save_unlocked();
        return id;
    }

    std::optional<StreamTemplate> find(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& t : templates_) {
            if (t.id == id) return t;
        }
        return std::nullopt;
    }

    void remove(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        templates_.erase(
            std::remove_if(templates_.begin(), templates_.end(), [&](const StreamTemplate& t) { return t.id == id; }),
            templates_.end());
        save_unlocked();
    }

private:
    std::mutex mutex_;
    std::vector<StreamTemplate> templates_;
};

}
