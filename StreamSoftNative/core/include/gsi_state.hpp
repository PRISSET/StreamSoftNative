#pragma once

#include <crow/json.h>
#include <crow/logging.h>

#include <array>
#include <chrono>
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
    std::string mode;
    int round = 0;
    int ct_score = 0;
    int t_score = 0;
};

// Parses CS2 Game State Integration payloads — POSTed locally by the game
// client itself once the .cfg from steam_paths.hpp is installed — and
// tracks match phase transitions to drive the "live now" section of the
// Faceit overlay widget and the viewer betting window. This is Valve's own
// real-time mechanism, not the Faceit API, so latency is as low as the game
// itself allows and it works for any match (matchmaking or Faceit), not
// just Faceit ones.
class GsiState {
public:
    void set_match_start_callback(std::function<void()> cb) { on_match_start_ = std::move(cb); }
    void set_match_lock_callback(std::function<void()> cb) { on_match_lock_ = std::move(cb); }
    void set_match_end_callback(std::function<void(bool player_won)> cb) { on_match_end_ = std::move(cb); }
    // Fired when a previously-live match stops sending GSI updates entirely
    // (game closed, crashed, or the player quit into a mode we don't track)
    // instead of ever reaching a clean "gameover" — see tick_staleness().
    void set_match_abort_callback(std::function<void()> cb) { on_match_abort_ = std::move(cb); }

    void set_lock_round(int round) {
        std::lock_guard<std::mutex> lock(mutex_);
        lock_round_ = round < 1 ? 1 : round;
    }

    bool update(const crow::json::rvalue& body) {
        std::function<void()> fire_start;
        std::function<void()> fire_lock;
        std::function<void(bool)> fire_end;
        bool win_result = false;
        bool changed = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!body.has("map")) return false;
            auto map = body["map"];
            std::string phase = map.has("phase") ? std::string(map["phase"].s()) : "";
            if (phase.empty()) return false;

            if (map.has("mode")) map_.mode = std::string(map["mode"].s());
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

            // Deathmatch/Casual/etc. keep sending GSI updates too, but have
            // no meaningful round score and shouldn't be treated as a "live
            // match" at all — a blocklist (rather than only allowing
            // "competitive") is deliberate: we don't actually know what mode
            // string Faceit's own servers report, so defaulting to "tracked"
            // for anything not explicitly known-irrelevant is safer than
            // silently failing to show a real Faceit match.
            if (!is_excluded_mode(map_.mode)) {
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
                        rounds_seen_live_ = 0;
                        last_seen_round_ = -1;
                        betting_open_ = true;
                    }
                } else if (phase == "live") {
                    // Don't assume round numbering (0- vs 1-indexed) — just
                    // count distinct round values seen while live this
                    // match, so "lock at round N" means "the Nth time the
                    // round number changes", regardless of what CS2 calls
                    // round one.
                    if (map_.round != last_seen_round_) {
                        last_seen_round_ = map_.round;
                        rounds_seen_live_++;
                    }
                    if (rounds_seen_live_ >= lock_round_ && !bet_locked_this_match_) {
                        fire_lock = on_match_lock_;
                        bet_locked_this_match_ = true;
                        betting_open_ = false;
                    }
                }
                last_phase_ = phase;
                last_update_epoch_ = current_epoch();
                changed = true;
            }
        }

        if (fire_start) fire_start();
        if (fire_lock) fire_lock();
        if (fire_end) fire_end(win_result);
        return changed;
    }

    // GSI is push-only — the game POSTs on change plus a heartbeat (see
    // "heartbeat" in the installed .cfg, currently 30s). If those updates
    // stop arriving mid-match (crash, alt-tab into a mode we don't track,
    // cancelled/abandoned matchmaking, someone not joining the lobby), the
    // widget would otherwise be stuck showing "live" forever and never
    // return to the recent-matches strip. Call this periodically from a
    // background timer; it resets state and fires the abort callback once
    // too long has passed since the last relevant update.
    bool tick_staleness(int timeout_seconds) {
        std::function<void()> fire_abort;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bool active = !last_phase_.empty() && last_phase_ != "gameover";
            if (active && last_update_epoch_ > 0 && current_epoch() - last_update_epoch_ > timeout_seconds) {
                fire_abort = on_match_abort_;
                last_phase_.clear();
                betting_open_ = false;
                bet_locked_this_match_ = false;
                rounds_seen_live_ = 0;
                last_seen_round_ = -1;
                changed = true;
            }
        }
        if (fire_abort) fire_abort();
        return changed;
    }

    crow::json::wvalue snapshot_json() {
        std::lock_guard<std::mutex> lock(mutex_);
        crow::json::wvalue j;
        j["type"] = "cs2_live";
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
        j["lockRound"] = lock_round_;
        return j;
    }

private:
    static long long current_epoch() {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    static bool is_excluded_mode(const std::string& mode) {
        static constexpr std::array<const char*, 8> kExcluded = {
            "deathmatch", "casual",     "custom",   "gungameprogressive",
            "gungametrbomb", "cooperative", "coopstrike", "survival"};
        for (auto m : kExcluded) {
            if (mode == m) return true;
        }
        return false;
    }

    std::mutex mutex_;
    MapState map_;
    PlayerState player_;
    std::string last_phase_;
    bool bet_locked_this_match_ = false;
    bool betting_open_ = false;
    int rounds_seen_live_ = 0;
    int last_seen_round_ = -1;
    int lock_round_ = 3;
    long long last_update_epoch_ = 0;

    std::function<void()> on_match_start_;
    std::function<void()> on_match_lock_;
    std::function<void(bool)> on_match_end_;
    std::function<void()> on_match_abort_;
};

}
