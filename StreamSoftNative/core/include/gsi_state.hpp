#pragma once

#include <crow/json.h>
#include <crow/logging.h>

#include <functional>
#include <mutex>
#include <string>

namespace streamsoft::cs2 {

struct PlayerState {
    std::string name;
    std::string team;
    int health = 0;
    int armor = 0;
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    int round_kills = 0;
};

struct MapState {
    std::string name;
    int round = 0;
    int ct_score = 0;
    int t_score = 0;
};

// Parses CS2 Game State Integration payloads — POSTed locally by the game
// client itself once the .cfg from steam_paths.hpp is installed — and
// tracks match phase transitions to drive the live HUD overlay and the
// viewer betting window. This is Valve's own real-time mechanism, not the
// Faceit API, so latency is as low as the game itself allows and it works
// for any match (matchmaking or Faceit), not just Faceit ones.
class GsiState {
public:
    void set_match_start_callback(std::function<void()> cb) { on_match_start_ = std::move(cb); }
    void set_match_lock_callback(std::function<void()> cb) { on_match_lock_ = std::move(cb); }
    void set_match_end_callback(std::function<void(bool player_won)> cb) { on_match_end_ = std::move(cb); }

    bool update(const crow::json::rvalue& body) {
        std::function<void()> fire_start;
        std::function<void()> fire_lock;
        std::function<void(bool)> fire_end;
        bool win_result = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!body.has("map")) return false;
            auto map = body["map"];
            std::string phase = map.has("phase") ? std::string(map["phase"].s()) : "";
            if (phase.empty()) return false;

            if (map.has("name")) map_.name = std::string(map["name"].s());
            if (map.has("round")) map_.round = static_cast<int>(map["round"].i());
            if (map.has("team_ct") && map["team_ct"].has("score"))
                map_.ct_score = static_cast<int>(map["team_ct"]["score"].i());
            if (map.has("team_t") && map["team_t"].has("score"))
                map_.t_score = static_cast<int>(map["team_t"]["score"].i());

            if (body.has("player")) {
                auto p = body["player"];
                if (p.has("name")) player_.name = std::string(p["name"].s());
                if (p.has("team")) player_.team = std::string(p["team"].s());
                if (p.has("state")) {
                    auto st = p["state"];
                    if (st.has("health")) player_.health = static_cast<int>(st["health"].i());
                    if (st.has("armor")) player_.armor = static_cast<int>(st["armor"].i());
                    if (st.has("round_kills")) player_.round_kills = static_cast<int>(st["round_kills"].i());
                }
                if (p.has("match_stats")) {
                    auto ms = p["match_stats"];
                    if (ms.has("kills")) player_.kills = static_cast<int>(ms["kills"].i());
                    if (ms.has("deaths")) player_.deaths = static_cast<int>(ms["deaths"].i());
                    if (ms.has("assists")) player_.assists = static_cast<int>(ms["assists"].i());
                }
            }

            if (phase == "gameover") {
                if (last_phase_ != "gameover") {
                    win_result = player_.team == "CT" ? (map_.ct_score > map_.t_score) : (map_.t_score > map_.ct_score);
                    fire_end = on_match_end_;
                    betting_open_ = false;
                }
            } else if (phase == "warmup") {
                if (last_phase_.empty() || last_phase_ == "gameover") {
                    fire_start = on_match_start_;
                    bet_locked_this_match_ = false;
                    betting_open_ = true;
                }
            } else if (phase == "live") {
                if (last_phase_ == "warmup" && !bet_locked_this_match_) {
                    fire_lock = on_match_lock_;
                    bet_locked_this_match_ = true;
                    betting_open_ = false;
                }
            }
            last_phase_ = phase;
        }

        if (fire_start) fire_start();
        if (fire_lock) fire_lock();
        if (fire_end) fire_end(win_result);
        return true;
    }

    crow::json::wvalue snapshot_json() {
        std::lock_guard<std::mutex> lock(mutex_);
        crow::json::wvalue j;
        j["type"] = "cs2_hud";
        j["active"] = !last_phase_.empty() && last_phase_ != "gameover";
        j["phase"] = last_phase_;
        j["map"] = map_.name;
        j["round"] = map_.round;
        j["ctScore"] = map_.ct_score;
        j["tScore"] = map_.t_score;
        j["playerTeam"] = player_.team;
        j["playerName"] = player_.name;
        j["health"] = player_.health;
        j["armor"] = player_.armor;
        j["kills"] = player_.kills;
        j["deaths"] = player_.deaths;
        j["assists"] = player_.assists;
        j["bettingOpen"] = betting_open_;
        return j;
    }

private:
    std::mutex mutex_;
    MapState map_;
    PlayerState player_;
    std::string last_phase_;
    bool bet_locked_this_match_ = false;
    bool betting_open_ = false;

    std::function<void()> on_match_start_;
    std::function<void()> on_match_lock_;
    std::function<void(bool)> on_match_end_;
};

}
