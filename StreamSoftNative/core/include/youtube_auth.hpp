#pragma once

#include "twitch_auth.hpp"

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

// Google OAuth 2.0 Device Authorization Grant — same shape as
// twitch_auth.hpp's device code flow, but Google's endpoints/params differ
// just enough (client_secret is mandatory here, Twitch has none; HTTP 428
// for "still waiting" instead of a plain error body) that it isn't a drop-in
// reuse of that file, just the same pattern applied to a second provider.
// Needed only for actions the read-only YouTube Data API key
// (youtube_chat.hpp) can't do — right now that's just updating a live
// broadcast's title from a stream template (see overlay_server.hpp).
namespace streamsoft::youtube_auth {

inline const std::string kAuthHost = "https://oauth2.googleapis.com";
inline const std::string kApiHost = "https://www.googleapis.com";
// "Manage your YouTube account" — the narrower youtube.force-ssl scope also
// covers broadcasts.update, but this one is what Google's own examples use
// for TV/limited-input device apps, so it's the one users are told to
// enable at the OAuth consent screen.
inline const std::string kScope = "https://www.googleapis.com/auth/youtube";

struct AuthPromptState {
    std::mutex mutex;
    bool pending = false;
    std::string verification_url;
    std::string user_code;

    std::string last_result;
    std::string last_error;
};

inline AuthPromptState& auth_prompt_state() {
    static AuthPromptState state;
    return state;
}

class ScopedAuthPrompt {
public:
    ScopedAuthPrompt(std::string verification_url, std::string user_code) {
        auto& s = auth_prompt_state();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.pending = true;
        s.verification_url = std::move(verification_url);
        s.user_code = std::move(user_code);
    }
    ~ScopedAuthPrompt() {
        auto& s = auth_prompt_state();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.pending = false;
        s.verification_url.clear();
        s.user_code.clear();
    }
    ScopedAuthPrompt(const ScopedAuthPrompt&) = delete;
    ScopedAuthPrompt& operator=(const ScopedAuthPrompt&) = delete;
};

inline const std::string kTokenFile = "youtube_token.json";

inline void invalidate_cached_token() { std::remove(kTokenFile.c_str()); }

struct Token {
    std::string access_token;
    std::string refresh_token;
    bool valid() const { return !access_token.empty(); }
};

inline Token parse_token(const crow::json::rvalue& j) {
    Token t;
    t.access_token = j.has("access_token") ? std::string(j["access_token"].s()) : "";
    t.refresh_token = j.has("refresh_token") ? std::string(j["refresh_token"].s()) : "";
    return t;
}

inline void save_token(const Token& t) {
    crow::json::wvalue j;
    j["access_token"] = t.access_token;
    j["refresh_token"] = t.refresh_token;
    std::ofstream f(kTokenFile, std::ios::binary | std::ios::trunc);
    f << j.dump();
}

inline bool load_cached(Token& out) {
    std::ifstream f(kTokenFile, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    auto parsed = crow::json::load(ss.str());
    if (!parsed) return false;
    out = parse_token(parsed);
    return out.valid();
}

struct AuthRejected : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline Token device_code_flow(const std::string& client_id, const std::string& client_secret) {
    auto auth = twitch::make_https_client(kAuthHost);

    httplib::Params device_params{{"client_id", client_id}, {"scope", kScope}};
    auto resp = auth.Post("/device/code", device_params);
    if (!resp || resp->status != 200) {
        throw std::runtime_error("Не удалось начать авторизацию YouTube: " + (resp ? resp->body : "no response"));
    }

    auto data = crow::json::load(resp->body);
    std::string verification_url = std::string(data["verification_url"].s());
    std::string user_code = std::string(data["user_code"].s());
    std::string device_code = std::string(data["device_code"].s());
    int interval = data.has("interval") ? static_cast<int>(data["interval"].i()) : 5;
    long long expires_in = data.has("expires_in") ? static_cast<long long>(data["expires_in"].i()) : 1800;

    ScopedAuthPrompt prompt(verification_url, user_code);

    CROW_LOG_INFO << "=== Авторизация YouTube ===";
    CROW_LOG_INFO << "Откройте в браузере: " << verification_url;
    CROW_LOG_INFO << "И введите код: " << user_code;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(expires_in);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));

        httplib::Params poll_params{
            {"client_id", client_id},
            {"client_secret", client_secret},
            {"device_code", device_code},
            {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
        };
        auto poll_resp = auth.Post("/token", poll_params);
        if (!poll_resp) continue;

        if (poll_resp->status == 200) {
            return parse_token(crow::json::load(poll_resp->body));
        }

        auto payload = crow::json::load(poll_resp->body);
        std::string error = (payload && payload.has("error")) ? std::string(payload["error"].s()) : "";
        if (error != "authorization_pending" && error != "slow_down") {
            throw std::runtime_error("Ошибка авторизации YouTube: " + poll_resp->body);
        }
    }

    throw std::runtime_error("Время авторизации YouTube истекло, попробуйте снова");
}

inline Token refresh_token_flow(const std::string& client_id, const std::string& client_secret,
                                 const std::string& refresh_token) {
    auto auth = twitch::make_https_client(kAuthHost);
    httplib::Params params{
        {"client_id", client_id},
        {"client_secret", client_secret},
        {"refresh_token", refresh_token},
        {"grant_type", "refresh_token"},
    };
    auto resp = auth.Post("/token", params);
    if (!resp || resp->status != 200) {
        throw std::runtime_error("Не удалось обновить токен YouTube: " + (resp ? resp->body : "no response"));
    }
    Token t = parse_token(crow::json::load(resp->body));
    if (t.refresh_token.empty()) t.refresh_token = refresh_token;  // refresh responses omit it, reuse the old one
    return t;
}

// Same "always refresh on use" shape as twitch::get_access_token — simpler
// than tracking expiry, and Google refresh tokens are long-lived (except:
// an OAuth client left in "Testing" publishing status in Google Cloud
// Console gets 7-day-expiring refresh tokens — if this starts demanding
// re-auth every week, that's why; publish the OAuth consent screen to fix).
inline std::string get_access_token(const std::string& client_id, const std::string& client_secret) {
    Token cached;
    bool have_cached = load_cached(cached);

    if (have_cached) {
        try {
            Token refreshed = refresh_token_flow(client_id, client_secret, cached.refresh_token);
            save_token(refreshed);
            return refreshed.access_token;
        } catch (const std::exception& e) {
            CROW_LOG_WARNING << "Не удалось обновить токен YouTube, повторная авторизация: " << e.what();
        }
    }

    Token fresh = device_code_flow(client_id, client_secret);
    save_token(fresh);
    return fresh.access_token;
}

// The authoritative "is my channel live right now" check. Two attempts
// verified live against a real account before landing here:
//   1. mine=true + broadcastStatus=active -> HTTP 400 "Incompatible
//      parameters specified in the request: mine, broadcastStatus".
//   2. broadcastStatus=active alone (no mine) -> 200 OK but always empty,
//      even while genuinely live — that filter apparently doesn't imply
//      "mine" as a matching filter at all with an installed-app token.
// What actually works: mine=true alone (lists the account's broadcasts,
// most recent first) with status.lifeCycleStatus checked client-side for
// "live" — status.lifeCycleStatus values per Google's docs are id "live"
// while actually broadcasting (also testing/ready/complete etc, not what
// we want here). Instant and always correct once found, unlike
// youtube_chat.hpp's resolve_live_video_id() (a public search.list scan,
// used only when there's no OAuth token to work with) which can lag behind
// reality by however long YouTube's search index takes to catch up. Empty
// string back means not live. Requires OAuth — the plain API key used for
// reading chat can't call this (it's a private query).
inline std::string check_live_video_id(const std::string& access_token) {
    auto api = twitch::make_https_client(kApiHost);
    httplib::Headers headers{{"Authorization", "Bearer " + access_token}};

    auto resp = api.Get("/youtube/v3/liveBroadcasts?part=id,status&mine=true&broadcastType=all&maxResults=50", headers);
    if (!resp || resp->status != 200) {
        throw std::runtime_error("liveBroadcasts.list вернул " +
                                  (resp ? std::to_string(resp->status) + " " + resp->body : "нет ответа"));
    }
    auto data = crow::json::load(resp->body);
    if (!data || !data.has("items")) return "";
    for (const auto& item : data["items"]) {
        if (!item.has("status") || !item["status"].has("lifeCycleStatus")) continue;
        if (std::string(item["status"]["lifeCycleStatus"].s()) != "live") continue;
        if (!item.has("id")) continue;
        return std::string(item["id"].s());
    }
    return "";
}

// videos.update replaces the whole "snippet" part it's given — sending just
// {title: ...} would silently wipe description/categoryId/tags, so this
// fetches the current snippet first and only overwrites the one field
// (same read-modify-write shape obs_scene_file.hpp/obs_multirtmp.hpp already
// use for other "can't safely send a partial object" APIs).
inline bool update_video_title(const std::string& access_token, const std::string& video_id, const std::string& title) {
    auto api = twitch::make_https_client(kApiHost);
    httplib::Headers headers{{"Authorization", "Bearer " + access_token}};

    auto get_resp = api.Get(("/youtube/v3/videos?part=snippet&id=" + twitch::url_encode(video_id)).c_str(), headers);
    if (!get_resp || get_resp->status != 200) {
        CROW_LOG_WARNING << "Не удалось получить текущие данные видео YouTube: "
                          << (get_resp ? std::to_string(get_resp->status) + " " + get_resp->body : "нет ответа");
        return false;
    }

    nlohmann::json data;
    try {
        data = nlohmann::json::parse(get_resp->body);
    } catch (const std::exception& e) {
        CROW_LOG_WARNING << "YouTube: не удалось разобрать ответ videos.list: " << e.what();
        return false;
    }
    if (!data.contains("items") || data["items"].empty()) {
        CROW_LOG_WARNING << "YouTube: видео " << video_id << " не найдено";
        return false;
    }

    nlohmann::json snippet = data["items"][0]["snippet"];
    snippet["title"] = title;

    nlohmann::json body;
    body["id"] = video_id;
    body["snippet"] = snippet;

    auto put_resp = api.Put("/youtube/v3/videos?part=snippet", headers, body.dump(), "application/json");
    if (!put_resp || put_resp->status != 200) {
        CROW_LOG_WARNING << "Не удалось обновить название трансляции YouTube: "
                          << (put_resp ? std::to_string(put_resp->status) + " " + put_resp->body : "нет ответа");
        return false;
    }
    return true;
}

inline void run_manual_auth(const std::string& client_id, const std::string& client_secret) {
    auto& s = auth_prompt_state();
    try {
        std::string token = get_access_token(client_id, client_secret);
        (void)token;
        std::lock_guard<std::mutex> lock(s.mutex);
        s.last_result = "success";
        s.last_error.clear();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.last_result = "error";
        s.last_error = e.what();
    }
}

}
