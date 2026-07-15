#pragma once

#include "faceit_stats.hpp"
#include "twitch_auth.hpp"

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace streamsoft::faceit {

struct MatchResult {
    std::string match_id;
    long long finished_at = 0;
    bool win = false;
    std::string score;
    std::string map;
    int kills = -1;
    int deaths = -1;
};

struct LifetimeStats {
    bool valid = false;
    int matches = 0;
    int wins = 0;
    double win_rate = 0;
    double avg_kd = 0;
    double avg_headshots = 0;
    int longest_streak = 0;
    int current_streak = 0;
};

struct EloPoint {
    long long ts = 0;
    int elo = 0;
};

struct Snapshot {
    bool valid = false;
    std::string nickname;
    std::string avatar;
    int elo = 0;
    int skill_level = 0;
    std::vector<MatchResult> matches;
    LifetimeStats lifetime;
    std::vector<EloPoint> elo_history;
    bool has_elo_change_today = false;
    int elo_change_today = 0;
    std::string error;
};

// Polls the FACEIT Data API (open.faceit.com) in the background and caches
// the latest snapshot for the overlay widget to read. Any failure here
// (bad API key, FACEIT downtime, an unexpected response shape) is caught
// and logged instead of propagating — this is a best-effort third-party
// integration, not something that should ever be able to take the whole
// app down (same isolation philosophy as the TTS/RVC adapters).
class FaceitClient {
public:
    static constexpr const char* kHost = "https://open.faceit.com";
    static constexpr const char* kGame = "cs2";
    static constexpr int kPollSeconds = 90;
    static constexpr int kRetrySeconds = 60;
    static constexpr const char* kEloHistoryFile = "faceit_elo_history.json";
    static constexpr size_t kEloHistoryMax = 100;

    void start(const std::string& nickname, const std::string& api_key) {
        stop();
        load_elo_history();
        running_ = true;
        thread_ = std::thread([this, nickname, api_key] { run(nickname, api_key); });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    ~FaceitClient() { stop(); }

    void set_report_callback(std::function<void(const std::string& month, const std::string& text)> cb) {
        stats_.set_report_callback(std::move(cb));
    }

    void mark_stream_start() {
        int elo;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            elo = snapshot_.elo;
        }
        stats_.mark_stream_start(elo);
    }

    std::optional<StreamSummary> stream_end_summary() {
        int elo;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            elo = snapshot_.elo;
        }
        return stats_.stream_end_summary(elo);
    }

    crow::json::wvalue snapshot_json() {
        std::lock_guard<std::mutex> lock(mutex_);
        crow::json::wvalue j;
        j["valid"] = snapshot_.valid;
        j["nickname"] = snapshot_.nickname;
        j["avatar"] = snapshot_.avatar;
        j["elo"] = snapshot_.elo;
        j["skill_level"] = snapshot_.skill_level;
        j["has_elo_change_today"] = snapshot_.has_elo_change_today;
        j["elo_change_today"] = snapshot_.elo_change_today;
        j["error"] = snapshot_.error;

        std::vector<crow::json::wvalue> matches;
        for (const auto& m : snapshot_.matches) {
            crow::json::wvalue mj;
            mj["win"] = m.win;
            mj["score"] = m.score;
            mj["map"] = m.map;
            mj["kills"] = m.kills;
            mj["deaths"] = m.deaths;
            matches.push_back(std::move(mj));
        }
        j["matches"] = std::move(matches);

        crow::json::wvalue lt;
        lt["valid"] = snapshot_.lifetime.valid;
        lt["matches"] = snapshot_.lifetime.matches;
        lt["wins"] = snapshot_.lifetime.wins;
        lt["win_rate"] = snapshot_.lifetime.win_rate;
        lt["avg_kd"] = snapshot_.lifetime.avg_kd;
        lt["avg_headshots"] = snapshot_.lifetime.avg_headshots;
        lt["longest_streak"] = snapshot_.lifetime.longest_streak;
        lt["current_streak"] = snapshot_.lifetime.current_streak;
        j["lifetime"] = std::move(lt);

        std::vector<crow::json::wvalue> elo_hist;
        for (const auto& p : snapshot_.elo_history) {
            crow::json::wvalue pj;
            pj["ts"] = p.ts;
            pj["elo"] = p.elo;
            elo_hist.push_back(std::move(pj));
        }
        j["elo_history"] = std::move(elo_hist);

        return j;
    }

private:
    void run(const std::string& nickname, const std::string& api_key) {
        std::string player_id;
        while (running_) {
            try {
                if (player_id.empty()) {
                    player_id = resolve_player_id(nickname, api_key);
                    if (player_id.empty()) {
                        set_error("Не удалось найти игрока '" + nickname + "' на FACEIT");
                        sleep_for(kRetrySeconds);
                        continue;
                    }
                }
                refresh(player_id, api_key);
            } catch (const std::exception& e) {
                set_error(std::string("Ошибка опроса FACEIT: ") + e.what());
            } catch (...) {
                set_error("Ошибка опроса FACEIT (неизвестное исключение)");
            }
            sleep_for(kPollSeconds);
        }
    }

    void sleep_for(int seconds) {
        for (int i = 0; i < seconds && running_; ++i) std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    httplib::Client make_client(const std::string& api_key) {
        auto cli = twitch::make_https_client(kHost);
        cli.set_default_headers(httplib::Headers{{"Authorization", "Bearer " + api_key}});
        return cli;
    }

    // FACEIT sometimes returns numeric-looking fields as JSON strings rather
    // than numbers depending on the endpoint — read either shape instead of
    // assuming one and throwing on the other.
    static int as_int(const crow::json::rvalue& v) {
        if (v.t() == crow::json::type::String) return std::atoi(std::string(v.s()).c_str());
        return static_cast<int>(v.i());
    }
    static double as_double(const crow::json::rvalue& v) {
        if (v.t() == crow::json::type::String) return std::atof(std::string(v.s()).c_str());
        return v.d();
    }
    static std::string as_string(const crow::json::rvalue& v) {
        if (v.t() == crow::json::type::String) return std::string(v.s());
        if (v.t() == crow::json::type::Number) return std::to_string(as_int(v));
        return "";
    }

    void load_elo_history() {
        std::lock_guard<std::mutex> lock(mutex_);
        elo_history_.clear();
        std::ifstream f(kEloHistoryFile, std::ios::binary);
        if (!f) return;
        std::ostringstream ss;
        ss << f.rdbuf();
        auto arr = crow::json::load(ss.str());
        if (!arr) return;
        for (const auto& item : arr) {
            if (!item.has("ts") || !item.has("elo")) continue;
            EloPoint p;
            p.ts = static_cast<long long>(item["ts"].i());
            p.elo = as_int(item["elo"]);
            elo_history_.push_back(p);
        }
    }

    void save_elo_history_unlocked() {
        std::vector<crow::json::wvalue> arr;
        for (const auto& p : elo_history_) {
            crow::json::wvalue pj;
            pj["ts"] = p.ts;
            pj["elo"] = p.elo;
            arr.push_back(std::move(pj));
        }
        crow::json::wvalue root(std::move(arr));
        std::ofstream f(kEloHistoryFile, std::ios::binary | std::ios::trunc);
        f << root.dump();
    }

    // The public Data API doesn't expose historical per-match ELO deltas, so
    // the trend line is built up locally from whatever this app has actually
    // observed while running — it starts short and grows over time rather
    // than faking a full history.
    void record_elo(int elo) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!elo_history_.empty() && elo_history_.back().elo == elo) return;
        EloPoint p;
        p.ts = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        p.elo = elo;
        elo_history_.push_back(p);
        while (elo_history_.size() > kEloHistoryMax) elo_history_.erase(elo_history_.begin());
        save_elo_history_unlocked();
    }

    static long long start_of_today() {
        std::time_t now = std::time(nullptr);
        std::tm local_tm{};
#if defined(_WIN32)
        localtime_s(&local_tm, &now);
#else
        localtime_r(&now, &local_tm);
#endif
        local_tm.tm_hour = 0;
        local_tm.tm_min = 0;
        local_tm.tm_sec = 0;
        return static_cast<long long>(std::mktime(&local_tm));
    }

    // Elo change since local midnight — computed from whatever points we've
    // actually recorded (see record_elo), so it reads "unknown" rather than
    // 0 until there's at least one data point to compare against.
    void compute_elo_change_today(Snapshot& snap) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (elo_history_.empty()) return;
        long long today_start = start_of_today();

        int baseline = -1;
        for (auto it = elo_history_.rbegin(); it != elo_history_.rend(); ++it) {
            if (it->ts < today_start) {
                baseline = it->elo;
                break;
            }
        }
        if (baseline < 0) baseline = elo_history_.front().elo;

        snap.elo_change_today = snap.elo - baseline;
        snap.has_elo_change_today = true;
    }

    std::string resolve_player_id(const std::string& nickname, const std::string& api_key) {
        auto cli = make_client(api_key);
        auto resp = cli.Get("/data/v4/players?nickname=" + twitch::url_encode(nickname));
        if (!resp || resp->status != 200) {
            CROW_LOG_WARNING << "Faceit: не удалось найти игрока " << nickname << " ("
                              << (resp ? std::to_string(resp->status) : "no response") << ")";
            return "";
        }
        auto j = crow::json::load(resp->body);
        if (!j || !j.has("player_id")) return "";
        return std::string(j["player_id"].s());
    }

    void refresh(const std::string& player_id, const std::string& api_key) {
        auto cli = make_client(api_key);

        Snapshot snap;
        auto profile_resp = cli.Get(("/data/v4/players/" + player_id).c_str());
        if (!profile_resp || profile_resp->status != 200) {
            set_error("Faceit API недоступен (профиль)");
            return;
        }
        auto profile = crow::json::load(profile_resp->body);
        if (!profile) {
            set_error("Faceit: некорректный ответ (профиль)");
            return;
        }

        snap.nickname = profile.has("nickname") ? std::string(profile["nickname"].s()) : "";
        snap.avatar = profile.has("avatar") ? std::string(profile["avatar"].s()) : "";
        if (profile.has("games") && profile["games"].has(kGame)) {
            auto game = profile["games"][kGame];
            if (game.has("faceit_elo")) snap.elo = as_int(game["faceit_elo"]);
            if (game.has("skill_level")) snap.skill_level = as_int(game["skill_level"]);
        }
        if (snap.elo > 0) {
            record_elo(snap.elo);
            compute_elo_change_today(snap);
        }

        fetch_lifetime(cli, player_id, snap.lifetime);

        std::string history_path =
            "/data/v4/players/" + player_id + "/history?game=" + std::string(kGame) + "&offset=0&limit=5";
        auto history_resp = cli.Get(history_path.c_str());
        if (history_resp && history_resp->status == 200) {
            auto history = crow::json::load(history_resp->body);
            if (history && history.has("items")) {
                for (const auto& item : history["items"]) {
                    if (!item.has("results") || !item.has("teams") || !item.has("match_id")) continue;
                    std::string match_id = std::string(item["match_id"].s());
                    auto results = item["results"];
                    std::string winner = results.has("winner") ? std::string(results["winner"].s()) : "";

                    std::string my_faction;
                    for (const char* faction_key : {"faction1", "faction2"}) {
                        if (!item["teams"].has(faction_key)) continue;
                        auto team = item["teams"][faction_key];
                        if (!team.has("players")) continue;
                        bool found = false;
                        for (const auto& p : team["players"]) {
                            if (p.has("player_id") && std::string(p["player_id"].s()) == player_id) {
                                found = true;
                                break;
                            }
                        }
                        if (found) {
                            my_faction = faction_key;
                            break;
                        }
                    }
                    if (my_faction.empty()) continue;

                    MatchResult m;
                    m.match_id = match_id;
                    if (item.has("finished_at")) m.finished_at = static_cast<long long>(item["finished_at"].i());
                    m.win = (my_faction == winner);
                    if (results.has("score")) {
                        auto score = results["score"];
                        std::vector<std::string> parts;
                        for (const auto& key : score.keys()) parts.push_back(as_string(score[key]));
                        for (size_t i = 0; i < parts.size(); ++i) {
                            if (i) m.score += ":";
                            m.score += parts[i];
                        }
                    }
                    fetch_match_stats(cli, match_id, player_id, m);
                    snap.matches.push_back(std::move(m));
                }
            }
        }

        if (snap.elo > 0) {
            stats_.observe(snap.elo);
            std::vector<MatchLogEntry> log_entries;
            for (const auto& m : snap.matches) {
                if (m.match_id.empty()) continue;
                MatchLogEntry e;
                e.match_id = m.match_id;
                e.finished_at = m.finished_at;
                e.win = m.win;
                e.score = m.score;
                e.map = m.map;
                e.kills = m.kills;
                e.deaths = m.deaths;
                log_entries.push_back(std::move(e));
            }
            stats_.record_matches(log_entries);
        }

        snap.valid = true;
        std::lock_guard<std::mutex> lock(mutex_);
        snap.elo_history = elo_history_;
        snapshot_ = std::move(snap);
    }

    // Aggregate lifetime stats (win rate, average K/D, headshot %, streaks) —
    // these are FACEIT's own computed numbers, far more meaningful than
    // averaging just the last 5 matches ourselves.
    void fetch_lifetime(httplib::Client& cli, const std::string& player_id, LifetimeStats& out) {
        try {
            auto resp = cli.Get(("/data/v4/players/" + player_id + "/stats/" + kGame).c_str());
            if (!resp || resp->status != 200) return;
            auto stats = crow::json::load(resp->body);
            if (!stats || !stats.has("lifetime")) return;
            auto lt = stats["lifetime"];

            auto get = [&](const char* key) -> std::optional<crow::json::rvalue> {
                if (!lt.has(key)) return std::nullopt;
                return lt[key];
            };
            if (auto v = get("Matches")) out.matches = as_int(*v);
            if (auto v = get("Wins")) out.wins = as_int(*v);
            if (auto v = get("Win Rate %")) out.win_rate = as_double(*v);
            if (auto v = get("Average K/D Ratio")) out.avg_kd = as_double(*v);
            if (auto v = get("Average Headshots %")) out.avg_headshots = as_double(*v);
            if (auto v = get("Longest Win Streak")) out.longest_streak = as_int(*v);
            if (auto v = get("Current Win Streak")) out.current_streak = as_int(*v);
            out.valid = true;
        } catch (...) {
            // leave lifetime stats unset
        }
    }

    // Per-match K/D and map aren't part of the history endpoint — needs a
    // separate call per match. Best-effort: leaves fields at their defaults
    // if this fails or the response doesn't have the expected shape, it
    // should never take down the rest of the snapshot over one bad match.
    void fetch_match_stats(httplib::Client& cli, const std::string& match_id, const std::string& player_id,
                            MatchResult& out) {
        try {
            auto resp = cli.Get(("/data/v4/matches/" + match_id + "/stats").c_str());
            if (!resp || resp->status != 200) return;
            auto stats = crow::json::load(resp->body);
            if (!stats || !stats.has("rounds")) return;

            for (const auto& round : stats["rounds"]) {
                if (round.has("round_stats") && round["round_stats"].has("Map")) {
                    out.map = std::string(round["round_stats"]["Map"].s());
                }
                if (!round.has("teams")) continue;
                for (const auto& team : round["teams"]) {
                    if (!team.has("players")) continue;
                    for (const auto& p : team["players"]) {
                        if (!p.has("player_id") || std::string(p["player_id"].s()) != player_id) continue;
                        if (!p.has("player_stats")) continue;
                        auto pstats = p["player_stats"];
                        if (pstats.has("Kills")) out.kills = as_int(pstats["Kills"]);
                        if (pstats.has("Deaths")) out.deaths = as_int(pstats["Deaths"]);
                        return;
                    }
                }
            }
        } catch (...) {
            // leave kills/deaths/map at their defaults
        }
    }

    void set_error(const std::string& err) {
        CROW_LOG_WARNING << "Faceit: " << err;
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.error = err;
    }

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    Snapshot snapshot_;
    std::vector<EloPoint> elo_history_;
    FaceitStatsTracker stats_;
};

}
