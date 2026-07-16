#pragma once

#include <crow/json.h>
#include <crow/logging.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace streamsoft::faceit {

struct MatchLogEntry {
    std::string match_id;
    long long finished_at = 0;
    bool win = false;
    std::string score;
    std::string map;
    int kills = -1;
    int deaths = -1;
};

struct StreamSummary {
    int elo_before = 0;
    int elo_after = 0;
    int session_delta = 0;
    int day_delta = 0;
    int month_delta = 0;
    int year_delta = 0;
};

inline std::string format_signed(int v) { return (v >= 0 ? "+" : "") + std::to_string(v); }

inline std::string format_stream_summary(const StreamSummary& s) {
    std::ostringstream out;
    out << "🎮 Faceit — итог стрима\n";
    out << "Было: " << s.elo_before << " ELO\n";
    out << "За стрим: " << format_signed(s.session_delta) << "\n";
    out << "Итог дня: " << format_signed(s.day_delta) << "\n";
    out << "Итог месяца: " << format_signed(s.month_delta) << "\n";
    out << "Итог года: " << format_signed(s.year_delta) << "\n";
    out << "Стало: " << s.elo_after << " ELO";
    return out.str();
}

namespace detail {

inline std::tm local_time(std::time_t t) {
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

inline std::string month_key_of(long long epoch_seconds) {
    if (epoch_seconds <= 0) return "";
    std::tm tm = local_time(static_cast<std::time_t>(epoch_seconds));
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04d-%02d", tm.tm_year + 1900, tm.tm_mon + 1);
    return buf;
}

inline std::string day_label_of(long long epoch_seconds) {
    if (epoch_seconds <= 0) return "";
    std::tm tm = local_time(static_cast<std::time_t>(epoch_seconds));
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d.%02d", tm.tm_mday, tm.tm_mon + 1);
    return buf;
}

}

// Tracks Faceit ELO across day/month/year calendar boundaries and a rolling
// log of finished matches, persisted to disk so streak/monthly reporting
// survives app restarts. All bucketing uses local calendar time (matches how
// a streamer thinks about "today"/"this month"), not UTC.
class FaceitStatsTracker {
public:
    static constexpr const char* kStatsFile = "faceit_stats.json";
    static constexpr const char* kLogFile = "faceit_match_log.json";
    static constexpr long long kLogMaxAgeSeconds = 400LL * 24 * 3600;

    FaceitStatsTracker() { load(); }

    void set_report_callback(std::function<void(const std::string& month, const std::string& text)> cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        report_callback_ = std::move(cb);
    }

    void mark_stream_start(int elo) {
        std::lock_guard<std::mutex> lock(mutex_);
        session_start_elo_ = elo;
        has_session_ = true;
    }

    std::optional<StreamSummary> stream_end_summary(int elo_now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_session_) return std::nullopt;
        StreamSummary s;
        s.elo_before = session_start_elo_;
        s.elo_after = elo_now;
        s.session_delta = elo_now - session_start_elo_;
        s.day_delta = elo_now - day_elo_start_;
        s.month_delta = elo_now - month_elo_start_;
        s.year_delta = elo_now - year_elo_start_;
        has_session_ = false;
        return s;
    }

    // Rolls day/month/year ELO baselines forward on a calendar boundary, and
    // fires the report callback with the previous month's full match log the
    // first time a new month is observed.
    void observe(int elo) {
        std::string pending_month;
        std::string report_text;
        std::function<void(const std::string&, const std::string&)> cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::time_t now = std::time(nullptr);
            std::tm tm = detail::local_time(now);
            char day_buf[16];
            std::snprintf(day_buf, sizeof(day_buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            char month_buf[8];
            std::snprintf(month_buf, sizeof(month_buf), "%04d-%02d", tm.tm_year + 1900, tm.tm_mon + 1);
            char year_buf[8];
            std::snprintf(year_buf, sizeof(year_buf), "%04d", tm.tm_year + 1900);

            bool changed = false;
            if (day_date_ != day_buf) {
                day_date_ = day_buf;
                day_elo_start_ = elo;
                changed = true;
            }
            if (month_date_ != month_buf) {
                if (!month_date_.empty() && last_reported_month_ != month_date_) {
                    pending_month = month_date_;
                }
                month_date_ = month_buf;
                month_elo_start_ = elo;
                changed = true;
            }
            if (year_date_ != year_buf) {
                year_date_ = year_buf;
                year_elo_start_ = elo;
                changed = true;
            }

            if (!pending_month.empty()) {
                report_text = build_month_report_unlocked(pending_month);
                last_reported_month_ = pending_month;
            }
            if (changed) save_stats_unlocked();
            cb = report_callback_;
        }
        if (!pending_month.empty() && cb) cb(pending_month, report_text);
    }

    void record_matches(const std::vector<MatchLogEntry>& matches) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool changed = false;
        for (const auto& m : matches) {
            if (m.match_id.empty()) continue;
            bool known = std::any_of(log_.begin(), log_.end(),
                                      [&](const MatchLogEntry& e) { return e.match_id == m.match_id; });
            if (known) continue;
            log_.push_back(m);
            changed = true;
        }
        if (!changed) return;
        prune_log_unlocked();
        save_log_unlocked();
    }

private:
    std::string build_month_report_unlocked(const std::string& month) const {
        std::vector<const MatchLogEntry*> entries;
        for (const auto& e : log_) {
            if (detail::month_key_of(e.finished_at) == month) entries.push_back(&e);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const MatchLogEntry* a, const MatchLogEntry* b) { return a->finished_at < b->finished_at; });

        int wins = 0;
        for (const auto* e : entries) {
            if (e->win) ++wins;
        }

        std::ostringstream out;
        out << "📅 Faceit — итоги месяца " << month << "\n";
        out << "Матчей: " << entries.size() << " (побед: " << wins << ", поражений: " << (entries.size() - wins)
            << ")\n\n";
        for (const auto* e : entries) {
            out << (e->win ? "✅ " : "❌ ") << detail::day_label_of(e->finished_at);
            if (!e->map.empty()) out << " · " << e->map;
            if (!e->score.empty()) out << " · " << e->score;
            if (e->kills >= 0 && e->deaths >= 0) out << " · " << e->kills << "/" << e->deaths;
            out << "\n";
        }
        return out.str();
    }

    void prune_log_unlocked() {
        long long cutoff =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count() -
            kLogMaxAgeSeconds;
        log_.erase(
            std::remove_if(log_.begin(), log_.end(),
                            [&](const MatchLogEntry& e) { return e.finished_at > 0 && e.finished_at < cutoff; }),
            log_.end());
    }

    void load() {
        std::lock_guard<std::mutex> lock(mutex_);
        {
            std::ifstream f(kStatsFile, std::ios::binary);
            if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                auto j = crow::json::load(ss.str());
                if (j) {
                    if (j.has("day_date")) day_date_ = std::string(j["day_date"].s());
                    if (j.has("day_elo_start")) day_elo_start_ = static_cast<int>(j["day_elo_start"].i());
                    if (j.has("month_date")) month_date_ = std::string(j["month_date"].s());
                    if (j.has("month_elo_start")) month_elo_start_ = static_cast<int>(j["month_elo_start"].i());
                    if (j.has("year_date")) year_date_ = std::string(j["year_date"].s());
                    if (j.has("year_elo_start")) year_elo_start_ = static_cast<int>(j["year_elo_start"].i());
                    if (j.has("last_reported_month")) last_reported_month_ = std::string(j["last_reported_month"].s());
                }
            }
        }
        {
            std::ifstream f(kLogFile, std::ios::binary);
            if (!f) return;
            std::ostringstream ss;
            ss << f.rdbuf();
            auto arr = crow::json::load(ss.str());
            if (!arr) return;
            for (const auto& item : arr) {
                if (!item.has("match_id")) continue;
                MatchLogEntry e;
                e.match_id = std::string(item["match_id"].s());
                if (item.has("finished_at")) e.finished_at = static_cast<long long>(item["finished_at"].i());
                if (item.has("win")) e.win = item["win"].b();
                if (item.has("score")) e.score = std::string(item["score"].s());
                if (item.has("map")) e.map = std::string(item["map"].s());
                if (item.has("kills")) e.kills = static_cast<int>(item["kills"].i());
                if (item.has("deaths")) e.deaths = static_cast<int>(item["deaths"].i());
                log_.push_back(std::move(e));
            }
        }
    }

    void save_stats_unlocked() {
        crow::json::wvalue j;
        j["day_date"] = day_date_;
        j["day_elo_start"] = day_elo_start_;
        j["month_date"] = month_date_;
        j["month_elo_start"] = month_elo_start_;
        j["year_date"] = year_date_;
        j["year_elo_start"] = year_elo_start_;
        j["last_reported_month"] = last_reported_month_;
        std::ofstream f(kStatsFile, std::ios::binary | std::ios::trunc);
        f << j.dump();
    }

    void save_log_unlocked() {
        std::vector<crow::json::wvalue> arr;
        for (const auto& e : log_) {
            crow::json::wvalue ej;
            ej["match_id"] = e.match_id;
            ej["finished_at"] = e.finished_at;
            ej["win"] = e.win;
            ej["score"] = e.score;
            ej["map"] = e.map;
            ej["kills"] = e.kills;
            ej["deaths"] = e.deaths;
            arr.push_back(std::move(ej));
        }
        crow::json::wvalue root(std::move(arr));
        std::ofstream f(kLogFile, std::ios::binary | std::ios::trunc);
        f << root.dump();
    }

    std::mutex mutex_;
    std::function<void(const std::string&, const std::string&)> report_callback_;

    std::string day_date_;
    int day_elo_start_ = 0;
    std::string month_date_;
    int month_elo_start_ = 0;
    std::string year_date_;
    int year_elo_start_ = 0;
    std::string last_reported_month_;

    bool has_session_ = false;
    int session_start_elo_ = 0;

    std::vector<MatchLogEntry> log_;
};

}
