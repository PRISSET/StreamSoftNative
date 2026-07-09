#pragma once

// Twitch device-code OAuth flow + token cache, mirroring
// softforstream/twitch_auth.py (Python reference) — same endpoints, same
// cache file format (twitch_token.json), same scopes.

#include "app_paths.hpp"

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>

#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace streamsoft::twitch {

inline const std::string kAuthHost = "https://id.twitch.tv";
inline const std::string kApiHost = "https://api.twitch.tv";
inline const std::string kScopes = "chat:read chat:edit moderator:read:followers channel:read:subscriptions bits:read";

// device_code_flow() runs on the Twitch worker thread and, until now, only
// surfaced the one-time verification_uri/user_code via CROW_LOG_INFO — a
// GUI build (see gui/main.cpp's WIN32 subsystem) has no console, so that
// log line was completely invisible and nobody could actually finish
// connecting Twitch. This mutex-guarded snapshot is polled by
// GET /api/twitch/auth-status (see overlay_server.hpp) so the GUI can show
// a real banner instead.
struct AuthPromptState {
    std::mutex mutex;
    bool pending = false;
    std::string verification_uri;
    std::string user_code;

    // Outcome of the most recent manual auth attempt (see
    // run_manual_auth() / POST /api/twitch/start-auth) — "" while nothing's
    // run yet or a new attempt just started, "success"/"error" once it
    // finishes. Without this, the GUI could only see `pending` flip back to
    // false and had no way to tell "it worked" from "it failed" — the
    // banner just vanished either way.
    std::string last_result;
    std::string last_username;
    std::string last_error;
};

inline AuthPromptState& auth_prompt_state() {
    static AuthPromptState state;
    return state;
}

// RAII: sets the prompt on construction, clears it on destruction —
// device_code_flow() has multiple exit paths (success return, two throws),
// this guarantees the prompt never gets stuck showing a stale/expired code
// no matter which one fires.
class ScopedAuthPrompt {
public:
    ScopedAuthPrompt(std::string verification_uri, std::string user_code) {
        auto& s = auth_prompt_state();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.pending = true;
        s.verification_uri = std::move(verification_uri);
        s.user_code = std::move(user_code);
    }
    ~ScopedAuthPrompt() {
        auto& s = auth_prompt_state();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.pending = false;
        s.verification_uri.clear();
        s.user_code.clear();
    }
    ScopedAuthPrompt(const ScopedAuthPrompt&) = delete;
    ScopedAuthPrompt& operator=(const ScopedAuthPrompt&) = delete;
};
inline const std::string kTokenFile = "twitch_token.json";

// Every outbound HTTPS client in this project must verify certificates
// against the bundled CA store — cpp-httplib does NOT verify by default,
// which would silently downgrade every OAuth/API call to MITM-able.
inline httplib::Client make_https_client(const std::string& host) {
    httplib::Client cli(host);
    cli.set_ca_cert_path(resolve_resource_file("certs/cacert.pem", STREAMSOFT_CACERT_PATH).c_str());
    cli.enable_server_certificate_verification(true);
    cli.set_connection_timeout(10);
    return cli;
}

struct Token {
    std::string access_token;
    std::string refresh_token;
    std::vector<std::string> scope;
    long long expires_in = 0;
    long long obtained_at = 0; // unix seconds, set on save()

    bool valid() const { return !access_token.empty(); }
};

inline long long now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Minimal percent-encoding for query params — avoids relying on cpp-httplib's
// internal detail:: namespace, which isn't part of its stable public API.
inline std::string url_encode(const std::string& value) {
    std::ostringstream out;
    out << std::hex << std::uppercase;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out << c;
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return out.str();
}

inline Token parse_token(const crow::json::rvalue& j) {
    Token t;
    t.access_token = j.has("access_token") ? std::string(j["access_token"].s()) : "";
    t.refresh_token = j.has("refresh_token") ? std::string(j["refresh_token"].s()) : "";
    t.expires_in = j.has("expires_in") ? static_cast<long long>(j["expires_in"].i()) : 0;
    t.obtained_at = j.has("obtained_at") ? static_cast<long long>(j["obtained_at"].i()) : 0;
    if (j.has("scope")) {
        for (const auto& s : j["scope"]) {
            t.scope.push_back(std::string(s.s()));
        }
    }
    return t;
}

inline void save_token(const Token& t) {
    crow::json::wvalue j;
    j["access_token"] = t.access_token;
    j["refresh_token"] = t.refresh_token;
    j["expires_in"] = t.expires_in;
    j["obtained_at"] = now_seconds();

    std::vector<crow::json::wvalue> scopes;
    for (const auto& s : t.scope) scopes.push_back(s);
    j["scope"] = std::move(scopes);

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

inline bool has_required_scopes(const Token& t) {
    static const std::vector<std::string> required = [] {
        std::vector<std::string> v;
        std::istringstream iss(kScopes);
        std::string s;
        while (iss >> s) v.push_back(s);
        return v;
    }();
    for (const auto& req : required) {
        bool found = false;
        for (const auto& got : t.scope) {
            if (got == req) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

// Blocking: polls until the user completes the browser auth step, or throws
// std::runtime_error on timeout/rejection. Publishes the verification URL +
// code via ScopedAuthPrompt (for the GUI banner) and CROW_LOG_INFO (console
// builds) for the whole duration of the wait.
inline Token device_code_flow(const std::string& client_id) {
    auto auth = make_https_client(kAuthHost);

    httplib::Params device_params{{"client_id", client_id}, {"scopes", kScopes}};
    auto resp = auth.Post("/oauth2/device", device_params);
    if (!resp || resp->status != 200) {
        throw std::runtime_error("Не удалось начать авторизацию Twitch: " + (resp ? resp->body : "no response"));
    }

    auto data = crow::json::load(resp->body);
    std::string verification_uri = std::string(data["verification_uri"].s());
    std::string user_code = std::string(data["user_code"].s());
    std::string device_code = std::string(data["device_code"].s());
    int interval = data.has("interval") ? static_cast<int>(data["interval"].i()) : 5;
    long long expires_in = data.has("expires_in") ? static_cast<long long>(data["expires_in"].i()) : 1800;

    ScopedAuthPrompt prompt(verification_uri, user_code);

    CROW_LOG_INFO << "=== Авторизация Twitch ===";
    CROW_LOG_INFO << "Откройте в браузере: " << verification_uri;
    CROW_LOG_INFO << "И введите код: " << user_code;
    CROW_LOG_INFO << "Ожидание подтверждения...";

    long long deadline = now_seconds() + expires_in;
    while (now_seconds() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));

        httplib::Params poll_params{
            {"client_id", client_id},
            {"scopes", kScopes},
            {"device_code", device_code},
            {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
        };
        auto poll_resp = auth.Post("/oauth2/token", poll_params);
        if (!poll_resp) continue;

        auto payload = crow::json::load(poll_resp->body);
        if (poll_resp->status == 200) {
            return parse_token(payload);
        }

        std::string message = (payload && payload.has("message")) ? std::string(payload["message"].s()) : "";
        if (message != "authorization_pending" && message != "slow_down") {
            throw std::runtime_error("Ошибка авторизации Twitch: " + poll_resp->body);
        }
    }

    throw std::runtime_error("Время авторизации Twitch истекло, запустите программу снова");
}

inline Token refresh_token_flow(const std::string& client_id, const std::string& refresh_token) {
    auto auth = make_https_client(kAuthHost);

    httplib::Params params{
        {"client_id", client_id},
        {"refresh_token", refresh_token},
        {"grant_type", "refresh_token"},
    };
    auto resp = auth.Post("/oauth2/token", params);
    if (!resp || resp->status != 200) {
        throw std::runtime_error("Не удалось обновить токен Twitch: " + (resp ? resp->body : "no response"));
    }
    return parse_token(crow::json::load(resp->body));
}

inline std::string get_access_token(const std::string& client_id) {
    Token cached;
    bool have_cached = load_cached(cached);

    if (have_cached && !has_required_scopes(cached)) {
        CROW_LOG_INFO << "У сохранённого токена не хватает нужных прав — потребуется повторная авторизация Twitch";
        have_cached = false;
    }

    if (have_cached) {
        if (now_seconds() < cached.obtained_at + cached.expires_in - 60) {
            return cached.access_token;
        }
        try {
            Token refreshed = refresh_token_flow(client_id, cached.refresh_token);
            save_token(refreshed);
            return refreshed.access_token;
        } catch (const std::exception& e) {
            CROW_LOG_WARNING << "Не удалось обновить токен Twitch, повторная авторизация: " << e.what();
        }
    }

    Token fresh = device_code_flow(client_id);
    save_token(fresh);
    return fresh.access_token;
}

inline std::string get_username(const std::string& client_id, const std::string& access_token) {
    auto api = make_https_client(kApiHost);
    httplib::Headers headers{{"Client-Id", client_id}, {"Authorization", "Bearer " + access_token}};

    auto resp = api.Get("/helix/users", headers);
    if (!resp || resp->status != 200) {
        throw std::runtime_error("Не удалось получить имя пользователя Twitch: " + (resp ? resp->body : "no response"));
    }
    auto data = crow::json::load(resp->body);
    return std::string(data["data"][0]["login"].s());
}

inline std::string get_user_id(const std::string& client_id, const std::string& access_token, const std::string& login) {
    auto api = make_https_client(kApiHost);
    httplib::Headers headers{{"Client-Id", client_id}, {"Authorization", "Bearer " + access_token}};

    auto resp = api.Get("/helix/users?login=" + url_encode(login), headers);
    if (!resp || resp->status != 200) {
        throw std::runtime_error("Не удалось получить id канала " + login + ": " + (resp ? resp->body : "no response"));
    }
    auto data = crow::json::load(resp->body);
    if (data["data"].size() == 0) {
        throw std::runtime_error("Не удалось получить id канала " + login + ": пустой ответ");
    }
    return std::string(data["data"][0]["id"].s());
}

// Kicked off from POST /api/twitch/start-auth (see overlay_server.hpp) the
// moment the GUI saves a new Client ID — without this, the device-code
// prompt only ever appeared once the Twitch chat/EventSub worker threads
// happened to start, which only happens at the next full app launch.
// Records success/failure into auth_prompt_state() so the polling GUI can
// show a real outcome instead of the banner just disappearing either way.
inline void run_manual_auth(const std::string& client_id) {
    auto& s = auth_prompt_state();
    try {
        std::string token = get_access_token(client_id);
        std::string username = get_username(client_id, token);
        std::lock_guard<std::mutex> lock(s.mutex);
        s.last_result = "success";
        s.last_username = username;
        s.last_error.clear();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.last_result = "error";
        s.last_error = e.what();
    }
}

} // namespace streamsoft::twitch
