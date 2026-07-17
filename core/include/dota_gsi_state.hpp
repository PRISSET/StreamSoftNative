#pragma once

#include "twitch_auth.hpp"

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace streamsoft::dota {

struct GsiHeroState {
    int id = 0;
    std::string name;
    std::string icon;
    int level = 0;
    bool alive = true;
    int health = 0;
    int max_health = 0;
};

struct GsiPlayerState {
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int last_hits = 0;
    int denies = 0;
    int gpm = 0;
    int xpm = 0;
    std::string team;  // "radiant" | "dire"
};

struct GsiMapState {
    std::string game_state;
    long long matchid = 0;
    int radiant_score = 0;
    int dire_score = 0;
    std::string win_team;  // "team2" (radiant) | "team3" (dire)
    int game_time = 0;
};

// Parses Dota 2 Game State Integration payloads — POSTed locally by the game
// client itself once the .cfg from dota_steam_paths.hpp is installed. This
// is Valve's own local push mechanism, the same one gsi_state.hpp already
// uses for CS2 — it works the instant a match starts and needs no Steam
// account privacy setting, unlike the OpenDota API (opendota_client.hpp),
// which silently returns nothing for any account with "Expose Public Match
// Data" left at its (default) off setting.
class DotaGsiState {
public:
    void set_match_start_callback(std::function<void()> cb) { on_match_start_ = std::move(cb); }
    void set_match_end_callback(std::function<void(bool player_won)> cb) { on_match_end_ = std::move(cb); }
    void set_match_abort_callback(std::function<void()> cb) { on_match_abort_ = std::move(cb); }

    // Fetches the hero id -> localized name/icon reference table once, off
    // the request-handling thread, so the first live update doesn't stall
    // on a network round-trip. Safe to call more than once; only the first
    // call does any work.
    void load_heroes_async() {
        std::thread([this] {
            try {
                fetch_heroes();
            } catch (const std::exception& e) {
                CROW_LOG_WARNING << "Dota GSI: hero catalog fetch failed: " << e.what();
            } catch (...) {
                CROW_LOG_WARNING << "Dota GSI: hero catalog fetch failed (unknown exception)";
            }
        }).detach();
    }

    bool update(const crow::json::rvalue& body) {
        std::function<void()> fire_start;
        std::function<void(bool)> fire_end;
        bool win_result = false;
        bool changed = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!body.has("map")) return false;
            auto map = body["map"];
            std::string state = map.has("game_state") ? std::string(map["game_state"].s()) : "";
            if (state.empty()) return false;

            if (map.has("matchid")) {
                // Source-engine GSI sends 64-bit match ids as a quoted
                // string to dodge JS float-precision loss, but be tolerant
                // of a raw number too since that's not verified against a
                // live payload.
                auto mid = map["matchid"];
                try {
                    map_.matchid =
                        mid.t() == crow::json::type::Number ? mid.i() : std::stoll(std::string(mid.s()));
                } catch (...) {
                }
            }
            if (map.has("radiant_score")) map_.radiant_score = static_cast<int>(map["radiant_score"].i());
            if (map.has("dire_score")) map_.dire_score = static_cast<int>(map["dire_score"].i());
            if (map.has("win_team")) map_.win_team = std::string(map["win_team"].s());
            if (map.has("game_time")) map_.game_time = static_cast<int>(map["game_time"].i());
            map_.game_state = state;

            if (body.has("player")) {
                auto p = body["player"];
                if (p.has("kills")) player_.kills = static_cast<int>(p["kills"].i());
                if (p.has("deaths")) player_.deaths = static_cast<int>(p["deaths"].i());
                if (p.has("assists")) player_.assists = static_cast<int>(p["assists"].i());
                if (p.has("last_hits")) player_.last_hits = static_cast<int>(p["last_hits"].i());
                if (p.has("denies")) player_.denies = static_cast<int>(p["denies"].i());
                if (p.has("gpm")) player_.gpm = static_cast<int>(p["gpm"].i());
                if (p.has("xpm")) player_.xpm = static_cast<int>(p["xpm"].i());
                if (p.has("team_name")) player_.team = std::string(p["team_name"].s());
            }

            if (body.has("hero")) {
                auto h = body["hero"];
                if (h.has("id")) {
                    hero_.id = static_cast<int>(h["id"].i());
                    auto it = heroes_.find(hero_.id);
                    if (it != heroes_.end()) {
                        hero_.name = it->second.first;
                        hero_.icon = it->second.second;
                    }
                }
                if (h.has("level")) hero_.level = static_cast<int>(h["level"].i());
                if (h.has("alive")) hero_.alive = h["alive"].b();
                if (h.has("health")) hero_.health = static_cast<int>(h["health"].i());
                if (h.has("max_health")) hero_.max_health = static_cast<int>(h["max_health"].i());
            }

            // DOTA_GAMERULES_STATE_* — the ones that mean "a match is under
            // way in some form", vs. lobby/disconnect/postgame which aren't.
            bool in_progress = state == "DOTA_GAMERULES_STATE_HERO_SELECTION" ||
                                state == "DOTA_GAMERULES_STATE_STRATEGY_TIME" ||
                                state == "DOTA_GAMERULES_STATE_PRE_GAME" ||
                                state == "DOTA_GAMERULES_STATE_GAME_IN_PROGRESS";

            if (state == "DOTA_GAMERULES_STATE_POST_GAME") {
                if (last_state_ != "DOTA_GAMERULES_STATE_POST_GAME") {
                    win_result = !player_.team.empty() && !map_.win_team.empty() &&
                                 ((player_.team == "radiant" && map_.win_team == "team2") ||
                                  (player_.team == "dire" && map_.win_team == "team3"));
                    fire_end = on_match_end_;
                }
            } else if (in_progress) {
                if (last_state_.empty() || last_state_ == "DOTA_GAMERULES_STATE_POST_GAME" ||
                    last_state_ == "DOTA_GAMERULES_STATE_DISCONNECT") {
                    fire_start = on_match_start_;
                }
            }
            last_state_ = state;
            last_update_epoch_ = current_epoch();
            changed = true;
        }

        if (fire_start) fire_start();
        if (fire_end) fire_end(win_result);
        return changed;
    }

    // Same idea as GsiState::tick_staleness — GSI is push-only, so if the
    // client stops sending (crash, force-quit) mid-match we'd otherwise be
    // stuck showing "live" forever.
    bool tick_staleness(int timeout_seconds) {
        std::function<void()> fire_abort;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool active = !last_state_.empty() && last_state_ != "DOTA_GAMERULES_STATE_POST_GAME" &&
                          last_state_ != "DOTA_GAMERULES_STATE_DISCONNECT";
            if (active && last_update_epoch_ > 0 && current_epoch() - last_update_epoch_ > timeout_seconds) {
                fire_abort = on_match_abort_;
                last_state_.clear();
                changed = true;
            }
        }
        if (fire_abort) fire_abort();
        return changed;
    }

    crow::json::wvalue snapshot_json() {
        std::lock_guard<std::mutex> lock(mutex_);
        crow::json::wvalue j;
        j["type"] = "dota_live";
        j["active"] = !last_state_.empty() && last_state_ != "DOTA_GAMERULES_STATE_POST_GAME" &&
                      last_state_ != "DOTA_GAMERULES_STATE_DISCONNECT";
        j["gameState"] = last_state_;
        j["radiantScore"] = map_.radiant_score;
        j["direScore"] = map_.dire_score;
        j["gameTime"] = map_.game_time;
        j["heroId"] = hero_.id;
        j["heroName"] = hero_.name;
        j["heroIcon"] = hero_.icon;
        j["heroLevel"] = hero_.level;
        j["heroAlive"] = hero_.alive;
        j["kills"] = player_.kills;
        j["deaths"] = player_.deaths;
        j["assists"] = player_.assists;
        j["lastHits"] = player_.last_hits;
        j["denies"] = player_.denies;
        j["gpm"] = player_.gpm;
        j["xpm"] = player_.xpm;
        j["playerTeam"] = player_.team;
        return j;
    }

private:
    static constexpr const char* kHeroCdnHost = "https://cdn.cloudflare.steamstatic.com";
    static constexpr const char* kOpenDotaHost = "https://api.opendota.com";

    static long long current_epoch() {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    // Deliberately independent of OpenDotaClient's own hero cache
    // (opendota_client.hpp) — GSI must keep resolving hero names/icons even
    // when the user has no OpenDota account configured at all.
    void fetch_heroes() {
        auto cli = twitch::make_https_client(kOpenDotaHost);
        auto resp = cli.Get("/api/constants/heroes");
        if (!resp || resp->status != 200) return;
        auto j = crow::json::load(resp->body);
        if (!j) return;

        std::unordered_map<int, std::pair<std::string, std::string>> parsed;
        for (const auto& key : j.keys()) {
            auto h = j[key];
            int id = std::atoi(key.c_str());
            std::string name = h.has("localized_name") ? std::string(h["localized_name"].s()) : "";
            std::string icon = h.has("icon") ? (std::string(kHeroCdnHost) + std::string(h["icon"].s())) : "";
            parsed[id] = {std::move(name), std::move(icon)};
        }

        std::lock_guard<std::mutex> lock(mutex_);
        heroes_ = std::move(parsed);
    }

    std::mutex mutex_;
    GsiMapState map_;
    GsiPlayerState player_;
    GsiHeroState hero_;
    std::string last_state_;
    long long last_update_epoch_ = 0;
    std::unordered_map<int, std::pair<std::string, std::string>> heroes_;

    std::function<void()> on_match_start_;
    std::function<void(bool)> on_match_end_;
    std::function<void()> on_match_abort_;
};

}
