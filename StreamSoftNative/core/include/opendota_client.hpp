#pragma once

#include "twitch_auth.hpp"

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace streamsoft::dota {

struct HeroInfo {
    std::string localized_name;
    std::string icon_path;
};

struct MatchResult {
    long long match_id = 0;
    bool win = false;
    std::string hero_name;
    std::string hero_icon;
    int kills = -1;
    int deaths = -1;
    int assists = -1;
    int duration_min = -1;
};

struct Snapshot {
    bool valid = false;
    std::string personaname;
    std::string avatar;
    std::string rank_label;
    std::vector<MatchResult> matches;
    double win_rate = 0;
    double avg_kda = 0;
    std::string error;
};

// Herald..Immortal, tens digit of OpenDota's rank_tier — opt-in on the
// player's side, absent for private profiles (see refresh()).
inline std::string rank_tier_label(int tier) {
    static constexpr std::array<const char*, 9> kNames = {"",       "Herald",  "Guardian", "Crusader", "Archon",
                                                            "Legend", "Ancient", "Divine",   "Immortal"};
    int rank = tier / 10;
    int star = tier % 10;
    if (rank < 1 || rank > 8) return "";
    if (rank == 8) return kNames[8];
    return std::string(kNames[rank]) + " " + std::to_string(star);
}

// Polls the free OpenDota API in the background and caches the latest
// snapshot for the overlay widget to read — same shape/isolation philosophy
// as FaceitClient (faceit_client.hpp): any failure here is caught and
// logged, never allowed to take the app down.
class OpenDotaClient {
public:
    static constexpr const char* kHost = "https://api.opendota.com";
    static constexpr const char* kHeroCdnHost = "https://cdn.cloudflare.steamstatic.com";
    static constexpr int kPollSeconds = 90;
    static constexpr int kRetrySeconds = 60;

    void start(const std::string& account_id) {
        stop();
        running_ = true;
        thread_ = std::thread([this, account_id] { run(account_id); });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    ~OpenDotaClient() { stop(); }

    bool is_running() const { return running_; }

    crow::json::wvalue snapshot_json() {
        std::lock_guard<std::mutex> lock(mutex_);
        crow::json::wvalue j;
        j["valid"] = snapshot_.valid;
        j["personaname"] = snapshot_.personaname;
        j["avatar"] = snapshot_.avatar;
        j["rank_label"] = snapshot_.rank_label;
        j["win_rate"] = snapshot_.win_rate;
        j["avg_kda"] = snapshot_.avg_kda;
        j["error"] = snapshot_.error;

        std::vector<crow::json::wvalue> matches;
        for (const auto& m : snapshot_.matches) {
            crow::json::wvalue mj;
            mj["win"] = m.win;
            mj["hero_name"] = m.hero_name;
            mj["hero_icon"] = m.hero_icon;
            mj["kills"] = m.kills;
            mj["deaths"] = m.deaths;
            mj["assists"] = m.assists;
            mj["duration_min"] = m.duration_min;
            matches.push_back(std::move(mj));
        }
        j["matches"] = std::move(matches);
        return j;
    }

private:
    void run(const std::string& account_id) {
        while (running_) {
            try {
                if (heroes_.empty()) fetch_heroes();
                refresh(account_id);
            } catch (const std::exception& e) {
                set_error(std::string("Ошибка опроса OpenDota: ") + e.what());
            } catch (...) {
                set_error("Ошибка опроса OpenDota (неизвестное исключение)");
            }
            sleep_for(kPollSeconds);
        }
    }

    void sleep_for(int seconds) {
        for (int i = 0; i < seconds && running_; ++i) std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    httplib::Client make_client() { return twitch::make_https_client(kHost); }

    // Hero id -> name/icon, fetched once (the hero roster only changes on
    // new hero releases, a few times a year) so match rendering doesn't
    // need a per-match extra request the way Faceit's per-match K/D lookup
    // does.
    void fetch_heroes() {
        auto cli = make_client();
        auto resp = cli.Get("/api/constants/heroes");
        if (!resp || resp->status != 200) return;
        auto j = crow::json::load(resp->body);
        if (!j) return;
        for (const auto& key : j.keys()) {
            auto h = j[key];
            int id = std::atoi(key.c_str());
            HeroInfo info;
            if (h.has("localized_name")) info.localized_name = std::string(h["localized_name"].s());
            if (h.has("icon")) info.icon_path = std::string(h["icon"].s());
            heroes_[id] = std::move(info);
        }
    }

    void refresh(const std::string& account_id) {
        auto cli = make_client();

        Snapshot snap;
        auto profile_resp = cli.Get(("/api/players/" + account_id).c_str());
        if (!profile_resp) {
            set_error("OpenDota API недоступен (нет сети)");
            return;
        }
        if (profile_resp->status == 404) {
            // 404 here means OpenDota has never seen this account_id at all — not a service
            // outage. Overwhelmingly this is because "Expose Public Match Data" (Dota 2
            // client: Settings -> Social) is off, which is the default; Valve gates match
            // data behind it for every third party, OpenDota included, so switching data
            // sources would hit the same wall. The other possibility is a wrong account ID.
            set_error("Аккаунт не найден в OpenDota. Проверь ID и включи в Dota 2 "
                       "настройку \"Экспонировать данные публичных матчей\" "
                       "(Настройки -> Соц. функции), затем сыграй матч");
            return;
        }
        if (profile_resp->status != 200) {
            set_error("OpenDota API недоступен (профиль, HTTP " + std::to_string(profile_resp->status) + ")");
            return;
        }
        auto profile = crow::json::load(profile_resp->body);
        if (!profile) {
            set_error("OpenDota: некорректный ответ (профиль)");
            return;
        }
        if (profile.has("profile")) {
            auto p = profile["profile"];
            if (p.has("personaname")) snap.personaname = std::string(p["personaname"].s());
            if (p.has("avatarfull")) snap.avatar = std::string(p["avatarfull"].s());
        }
        if (profile.has("rank_tier") && profile["rank_tier"].t() == crow::json::type::Number) {
            snap.rank_label = rank_tier_label(static_cast<int>(profile["rank_tier"].i()));
        }

        auto matches_resp = cli.Get(("/api/players/" + account_id + "/recentMatches").c_str());
        if (matches_resp && matches_resp->status == 200) {
            auto arr = crow::json::load(matches_resp->body);
            if (arr) {
                int count = 0;
                for (const auto& m : arr) {
                    if (count >= 5) break;
                    if (!m.has("match_id") || !m.has("player_slot") || !m.has("radiant_win")) continue;

                    MatchResult r;
                    r.match_id = static_cast<long long>(m["match_id"].i());
                    int slot = static_cast<int>(m["player_slot"].i());
                    bool radiant_win = m["radiant_win"].b();
                    r.win = (slot < 128) == radiant_win;

                    if (m.has("hero_id")) {
                        auto it = heroes_.find(static_cast<int>(m["hero_id"].i()));
                        if (it != heroes_.end()) {
                            r.hero_name = it->second.localized_name;
                            if (!it->second.icon_path.empty()) r.hero_icon = std::string(kHeroCdnHost) + it->second.icon_path;
                        }
                    }
                    if (m.has("kills")) r.kills = static_cast<int>(m["kills"].i());
                    if (m.has("deaths")) r.deaths = static_cast<int>(m["deaths"].i());
                    if (m.has("assists")) r.assists = static_cast<int>(m["assists"].i());
                    if (m.has("duration")) r.duration_min = static_cast<int>(m["duration"].i()) / 60;

                    snap.matches.push_back(std::move(r));
                    ++count;
                }
            }
        }

        if (!snap.matches.empty()) {
            int wins = 0;
            double kda_sum = 0;
            int kda_count = 0;
            for (const auto& m : snap.matches) {
                if (m.win) ++wins;
                if (m.kills >= 0 && m.deaths >= 0 && m.assists >= 0) {
                    kda_sum += static_cast<double>(m.kills + m.assists) / std::max(1, m.deaths);
                    ++kda_count;
                }
            }
            snap.win_rate = 100.0 * wins / static_cast<double>(snap.matches.size());
            snap.avg_kda = kda_count > 0 ? kda_sum / kda_count : 0.0;
        }

        snap.valid = true;
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = std::move(snap);
    }

    void set_error(const std::string& err) {
        CROW_LOG_WARNING << "OpenDota: " << err;
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.error = err;
    }

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    Snapshot snapshot_;
    std::unordered_map<int, HeroInfo> heroes_;
};

}
