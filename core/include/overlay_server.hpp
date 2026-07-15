#pragma once

#include <crow.h>
#include <httplib.h>

#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include "auto_update.hpp"
#include "chat_commands.hpp"
#include "connections_config.hpp"
#include "faceit_client.hpp"
#include "faceit_shared_key.hpp"
#include "gif_store.hpp"
#include "gpu_check.hpp"
#include "moderation.hpp"
#include "module_installer.hpp"
#include "obs_client.hpp"
#include "obs_scene_file.hpp"
#include "outgoing_queue.hpp"
#include "points.hpp"
#include "poll.hpp"
#include "song_queue.hpp"
#include "runtime_settings.hpp"
#include "telegram.hpp"
#include "tts_worker.hpp"
#include "twitch_auth.hpp"
#include "yt_resolve.hpp"

namespace streamsoft {

class OverlayServer {
public:
    OverlayServer(int port, std::filesystem::path web_dir)
        : port_(port), web_dir_(std::move(web_dir)), media_dir_(web_dir_ / "media"), runtime_(RuntimeSettings::load()) {
        std::filesystem::create_directories(media_dir_);
        setup_routes();
    }

    void run() {
        CROW_LOG_INFO << "StreamSoft Native overlay: http://127.0.0.1:" << port_ << "/ (/chat, /events)";
        app_.port(port_).multithreaded().run();
    }

    bool is_muted(const std::string& author) { return moderation_.is_muted(author); }
    ModerationState& moderation() { return moderation_; }

    std::optional<std::string> match_command(const std::string& text) { return commands_.match(text); }

    bool try_poll_vote(const std::string& username, const std::string& text) {
        bool voted = poll_.try_vote(username, text);
        if (voted) broadcast_poll_update();
        return voted;
    }

    void broadcast_poll_update() {
        auto payload = poll_.status();
        payload["type"] = "poll";
        broadcast_raw(payload.dump());
    }

    void award_points_for_message(const std::string& username) {
        std::lock_guard<std::mutex> lock(runtime_mutex_);
        points_.award_for_message(username, runtime_.points_per_message);
    }

    void set_faceit_client(faceit::FaceitClient* client) { faceit_ = client; }

    void set_broadcaster_name(const std::string& name) { broadcaster_name_ = name; }

    void set_twitch_outgoing(OutgoingQueue* queue) { twitch_outgoing_ = queue; }
    OutgoingQueue* twitch_outgoing() const { return twitch_outgoing_; }

    void set_twitch_client_id(const std::string& client_id) { twitch_client_id_ = client_id; }

    std::optional<std::string> try_builtin_command(const std::string& username, const std::string& text) {
        std::string lower = trim(text);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "!points" || lower == "!баллы") {
            return username + ", у тебя " + std::to_string(points_.balance(username)) + " баллов";
        }

        if (lower == "!gifs" || lower == "!гифки") {
            auto list = gifs_.all();
            if (list.empty()) return username + ", гифок пока нет";
            std::string out = "Гифки: ";
            bool first = true;
            for (const auto& item : list) {
                if (!first) out += ", ";
                first = false;
                out += "!gif " + item.name + " (" + std::to_string(item.price) + ")";
            }
            return out;
        }

        if (lower == "!help" || lower == "!помощь") {
            bool song_enabled;
            int cost;
            {
                std::lock_guard<std::mutex> lock(runtime_mutex_);
                song_enabled = runtime_.song_requests_enabled;
                cost = runtime_.song_request_cost;
            }
            std::string help = "Команды: !points — баланс баллов. Во время опроса — !1, !2 и т.д. — проголосовать.";
            if (song_enabled) {
                help += " !song <ссылка YouTube/SoundCloud> — заказать музыку (" + std::to_string(cost) + " баллов).";
            }
            if (!gifs_.all().empty()) help += " !gif <имя> — включить гифку за баллы, список — !gifs.";
            if (!twitch_client_id_.empty()) help += " !clip — вырезать клип последних секунд стрима.";
            return help;
        }

        if (lower == "!clip" || lower == "!клип") {
            return try_create_clip(username);
        }

        return std::nullopt;
    }

    std::optional<std::string> try_create_clip(const std::string& username) {
        if (twitch_client_id_.empty() || broadcaster_name_.empty()) return std::nullopt;

        {
            std::lock_guard<std::mutex> lock(clip_mutex_);
            auto now = std::chrono::steady_clock::now();
            if (last_clip_time_ && std::chrono::duration_cast<std::chrono::seconds>(now - *last_clip_time_).count() <
                                       kClipCooldownSeconds) {
                return username + ", клип уже создаётся — подожди немного";
            }
            last_clip_time_ = now;
        }

        std::string client_id = twitch_client_id_;
        std::string channel = broadcaster_name_;
        std::thread([this, client_id, channel, username] {
            try {
                std::string token = twitch::get_access_token(client_id);
                std::string broadcaster_id = resolve_broadcaster_id(client_id, token, channel);
                auto clip_id = twitch::create_clip(client_id, token, broadcaster_id);
                if (clip_id) {
                    push_outgoing(username + ", клип готов: https://clips.twitch.tv/" + *clip_id);
                } else {
                    push_outgoing(username + ", не получилось создать клип (возможно, стрим сейчас офлайн)");
                }
            } catch (const std::exception& e) {
                CROW_LOG_WARNING << "!clip: ошибка создания клипа: " << e.what();
                push_outgoing(username + ", не получилось создать клип");
            }
        }).detach();

        return username + ", создаю клип…";
    }

    std::string resolve_broadcaster_id(const std::string& client_id, const std::string& token, const std::string& channel) {
        {
            std::lock_guard<std::mutex> lock(clip_mutex_);
            if (!twitch_broadcaster_id_.empty()) return twitch_broadcaster_id_;
        }
        std::string id = twitch::get_user_id(client_id, token, channel);
        std::lock_guard<std::mutex> lock(clip_mutex_);
        twitch_broadcaster_id_ = id;
        return id;
    }

    void push_outgoing(const std::string& text) {
        if (twitch_outgoing_) twitch_outgoing_->push(text);
    }

    std::optional<std::string> song_reminder_text() {
        std::lock_guard<std::mutex> lock(runtime_mutex_);
        if (!runtime_.song_requests_enabled) return std::nullopt;
        return "Пиши в чат — получаешь баллы за активность! На них можно заказать музыку: !song <ссылка "
               "YouTube/SoundCloud> (" +
               std::to_string(runtime_.song_request_cost) + " баллов). Проверить баланс — !points";
    }

    std::optional<std::string> try_song_request(const std::string& username, const std::string& text) {
        static const std::string kPrefix = "!song ";

        std::string trimmed = trim(text);
        if (trimmed.size() <= kPrefix.size()) return std::nullopt;
        std::string prefix_lower = trimmed.substr(0, kPrefix.size());
        std::transform(prefix_lower.begin(), prefix_lower.end(), prefix_lower.begin(), ::tolower);
        if (prefix_lower != kPrefix) return std::nullopt;

        bool enabled;
        int cost;
        {
            std::lock_guard<std::mutex> lock(runtime_mutex_);
            enabled = runtime_.song_requests_enabled;
            cost = runtime_.song_request_cost;
        }
        if (!enabled) return std::nullopt;

        std::string url = trimmed.substr(kPrefix.size());
        auto parsed = parse_song_link(url);
        if (!parsed.valid) {
            return std::string("Не похоже на ссылку YouTube или SoundCloud");
        }

        bool is_broadcaster = !broadcaster_name_.empty() && iequals(username, broadcaster_name_);
        if (!is_broadcaster && !points_.spend(username, cost)) {
            return "Недостаточно баллов (нужно " + std::to_string(cost) + ", у тебя " +
                   std::to_string(points_.balance(username)) + ")";
        }

        bool was_empty = !song_queue_.has_current();
        song_queue_.enqueue({parsed.platform, parsed.ref, username});
        if (was_empty) song_queue_.advance();
        broadcast_now_playing();

        return is_broadcaster ? std::string("Добавлено в очередь!")
                               : "Добавлено в очередь! Осталось баллов: " + std::to_string(points_.balance(username));
    }

    std::optional<std::string> try_gif_request(const std::string& username, const std::string& text) {
        static const std::string kPrefixEn = "!gif ";
        static const std::string kPrefixRu = "!гиф ";  // "!гиф " — 8 bytes in UTF-8, not 5

        std::string trimmed = trim(text);
        std::string lower = trimmed;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);  // ASCII-only; leaves UTF-8 bytes intact

        std::string prefix;
        if (lower.rfind(kPrefixEn, 0) == 0) prefix = kPrefixEn;
        else if (lower.rfind(kPrefixRu, 0) == 0)
            prefix = kPrefixRu;
        if (prefix.empty()) return std::nullopt;

        std::string name = GifStore::normalize(trimmed.substr(prefix.size()));
        auto entry = gifs_.find(name);
        bool has_gif = gif_file_exists(name, "gif");
        bool has_mp3 = gif_file_exists(name, "mp3");
        if (!entry || (!has_gif && !has_mp3)) {
            return username + ", такой гифки нет — список: !gifs";
        }

        bool is_broadcaster = !broadcaster_name_.empty() && iequals(username, broadcaster_name_);
        if (!is_broadcaster && entry->price > 0 && !points_.spend(username, entry->price)) {
            return "Недостаточно баллов (нужно " + std::to_string(entry->price) + ", у тебя " +
                   std::to_string(points_.balance(username)) + ")";
        }

        crow::json::wvalue payload;
        payload["type"] = "gif_alert";
        payload["name"] = entry->name;
        payload["hasGif"] = has_gif;
        payload["hasMp3"] = has_mp3;
        payload["user"] = username;
        broadcast_raw(payload.dump());

        return entry->price > 0
                   ? "Запускаю \"" + entry->name + "\"! Осталось баллов: " + std::to_string(points_.balance(username))
                   : "Запускаю \"" + entry->name + "\"!";
    }

    void broadcast_now_playing() { broadcast_raw(now_playing_payload().dump()); }

    bool try_youtube_fallback() {
        auto pending = song_queue_.begin_youtube_fallback();
        if (!pending) return false;
        std::string video_id = pending->first;
        int generation = pending->second;

        std::thread([this, video_id, generation] {
            auto direct_url = ytresolve::resolve_direct_audio_url("https://youtu.be/" + video_id);
            if (direct_url && song_queue_.apply_direct_url(generation, *direct_url)) {
                CROW_LOG_INFO << "song_queue: эмбед не сыграл, включаю " << video_id << " напрямую через yt-dlp";
                broadcast_now_playing();
            } else {
                if (!direct_url) CROW_LOG_WARNING << "song_queue: yt-dlp не смог резолвнуть " << video_id << ", пропускаю трек";
                song_queue_.advance();
                broadcast_now_playing();
            }
        }).detach();
        return true;
    }

    crow::json::wvalue now_playing_payload() {
        auto payload = song_queue_.status();
        payload["type"] = "now_playing";
        std::lock_guard<std::mutex> lock(runtime_mutex_);
        payload["volume"] = runtime_.song_request_volume;
        return payload;
    }

    void set_tts_worker(tts::TtsWorker* tts) { tts_ = tts; }

    void set_rvc_port(int port) { rvc_port_ = port; }

    void set_rvc_control(std::function<bool()> start_fn, std::function<void()> stop_fn) {
        rvc_start_ = std::move(start_fn);
        rvc_stop_ = std::move(stop_fn);
    }

    void broadcast_chat(const std::string& platform, const std::string& author, const std::string& text) {
        crow::json::wvalue payload;
        payload["type"] = "chat";
        payload["platform"] = platform;
        payload["author"] = author;
        payload["text"] = text;
        std::string data = payload.dump();

        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            chat_history_.push_back(data);
            while (chat_history_.size() > kChatHistorySize) {
                chat_history_.pop_front();
            }
        }
        broadcast_raw(data);
    }

    void broadcast_event(const std::string& kind, const std::string& user, const std::string& detail) {
        crow::json::wvalue payload;
        payload["type"] = "event";
        payload["kind"] = kind;
        payload["user"] = user;
        payload["detail"] = detail;
        broadcast_raw(payload.dump());
    }

private:
    static constexpr size_t kChatHistorySize = 30;
    static constexpr std::array<const char*, 5> kEventKinds = {"follow", "subscribe", "gift_sub", "raid", "cheer"};
    static constexpr std::array<const char*, 2> kMediaExts = {"gif", "mp3"};

    static bool is_known_kind(const std::string& kind) {
        for (auto k : kEventKinds)
            if (kind == k) return true;
        return false;
    }
    static bool is_known_ext(const std::string& ext) {
        for (auto e : kMediaExts)
            if (ext == e) return true;
        return false;
    }

    // Crow's <string> route segments arrive percent-encoded but NOT
    // auto-decoded (unlike query-string params) — without this, a gif name
    // typed in Cyrillic on the GUI's upload/delete URL path ends up stored
    // under its literal "%d0%b9"-style encoded form instead of matching the
    // entry created via the (already-decoded) JSON POST /api/gifs body.
    static std::string url_decode_segment(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
                out.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)));
                i += 2;
            } else {
                out.push_back(s[i]);
            }
        }
        return out;
    }

    static bool is_safe_segment(const std::string& s) {
        return !s.empty() && s.find("..") == std::string::npos && s.find('/') == std::string::npos &&
               s.find('\\') == std::string::npos;
    }

    static bool iequals(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        return std::equal(a.begin(), a.end(), b.begin(),
                           [](unsigned char x, unsigned char y) { return ::tolower(x) == ::tolower(y); });
    }

    static std::string trim(const std::string& s) {
        auto not_space = [](unsigned char c) { return !std::isspace(c); };
        auto start = std::find_if(s.begin(), s.end(), not_space);
        auto end = std::find_if(s.rbegin(), s.rend(), not_space).base();
        return start < end ? std::string(start, end) : std::string();
    }

    void setup_routes() {
        CROW_ROUTE(app_, "/")
        ([this] { return serve_file(web_dir_, "index.html"); });

        CROW_ROUTE(app_, "/chat")
        ([this] { return serve_file(web_dir_, "chat.html"); });

        CROW_ROUTE(app_, "/events")
        ([this] { return serve_file(web_dir_, "events.html"); });

        CROW_ROUTE(app_, "/poll")
        ([this] { return serve_file(web_dir_, "poll.html"); });

        CROW_ROUTE(app_, "/nowplaying")
        ([this] { return serve_file(web_dir_, "nowplaying.html"); });

        CROW_ROUTE(app_, "/faceit")
        ([this] {
            // OBS's embedded Chromium (CEF) caches the top-level page across
            // the whole time the browser source is loaded — without this, a
            // rebuilt faceit.html (new layout/theme logic) keeps running the
            // OLD cached script until the source is manually reloaded.
            auto res = serve_file(web_dir_, "faceit.html");
            res.set_header("Cache-Control", "no-store");
            return res;
        });

        CROW_ROUTE(app_, "/static/<string>")
        ([this](const std::string& filename) {
            if (!is_safe_segment(filename)) return crow::response(404);
            return serve_file(web_dir_ / "static", filename);
        });

        CROW_ROUTE(app_, "/media/<string>")
        ([this](const std::string& filename) {
            if (!is_safe_segment(filename)) return crow::response(404);
            return serve_file(media_dir_, filename);
        });

        setup_settings_routes();
        setup_media_routes();
        setup_commands_routes();
        setup_rvc_routes();
        setup_modules_routes();
        setup_updates_routes();
        setup_poll_routes();
        setup_songqueue_routes();
        setup_obs_routes();
        setup_connections_routes();
        setup_gifs_routes();
        setup_faceit_routes();
        setup_test_routes();
        setup_ws_route();
    }

    void setup_connections_routes() {
        CROW_ROUTE(app_, "/api/connections")
            .methods(crow::HTTPMethod::Get)([this](const crow::request&) {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                auto c = ConnectionsConfig::load();
                return crow::response(c.to_json().dump());
            });

        CROW_ROUTE(app_, "/api/connections")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                auto body = crow::json::load(req.body);
                if (!body) return crow::response(400, R"({"ok": false, "error": "bad json"})");

                std::lock_guard<std::mutex> lock(connections_mutex_);
                auto c = ConnectionsConfig::load();
                if (body.has("twitch_client_id")) c.twitch_client_id = std::string(body["twitch_client_id"].s());
                if (body.has("twitch_channel"))
                    c.twitch_channel = normalize_twitch_channel(std::string(body["twitch_channel"].s()));
                if (body.has("twitch_chat_enabled")) c.twitch_chat_enabled = body["twitch_chat_enabled"].b();
                if (body.has("twitch_eventsub_enabled"))
                    c.twitch_eventsub_enabled = body["twitch_eventsub_enabled"].b();
                if (body.has("youtube_api_key")) c.youtube_api_key = std::string(body["youtube_api_key"].s());
                if (body.has("youtube_video_id")) c.youtube_video_id = std::string(body["youtube_video_id"].s());
                if (body.has("youtube_enabled")) c.youtube_enabled = body["youtube_enabled"].b();
                if (body.has("telegram_bot_token")) c.telegram_bot_token = std::string(body["telegram_bot_token"].s());
                if (body.has("telegram_chat_id")) c.telegram_chat_id = std::string(body["telegram_chat_id"].s());
                if (body.has("telegram_enabled")) c.telegram_enabled = body["telegram_enabled"].b();
                if (body.has("telegram_control_enabled"))
                    c.telegram_control_enabled = body["telegram_control_enabled"].b();
                if (body.has("social_telegram_channel_id"))
                    c.social_telegram_channel_id = std::string(body["social_telegram_channel_id"].s());
                if (body.has("social_telegram_enabled"))
                    c.social_telegram_enabled = body["social_telegram_enabled"].b();
                if (body.has("faceit_nickname")) c.faceit_nickname = std::string(body["faceit_nickname"].s());
                if (body.has("faceit_api_key")) c.faceit_api_key = std::string(body["faceit_api_key"].s());
                if (body.has("faceit_enabled")) c.faceit_enabled = body["faceit_enabled"].b();
                if (body.has("faceit_stats_telegram_enabled"))
                    c.faceit_stats_telegram_enabled = body["faceit_stats_telegram_enabled"].b();
                if (faceit_ && (body.has("faceit_nickname") || body.has("faceit_api_key") || body.has("faceit_enabled"))) {
                    if (c.should_run_faceit()) {
                        std::string api_key = c.faceit_api_key.empty() ? faceit::shared_api_key() : c.faceit_api_key;
                        faceit_->start(c.faceit_nickname, api_key);
                    } else {
                        faceit_->stop();
                    }
                }
                if (faceit_ && (body.has("faceit_stats_telegram_enabled") || body.has("social_telegram_channel_id") ||
                                 body.has("telegram_bot_token") || body.has("faceit_nickname") ||
                                 body.has("faceit_enabled"))) {
                    if (c.should_post_faceit_stats()) {
                        std::string bot_token = c.telegram_bot_token;
                        std::string channel_id = c.social_telegram_channel_id;
                        faceit_->set_report_callback([bot_token, channel_id](const std::string&, const std::string& text) {
                            std::thread([bot_token, channel_id, text] {
                                telegram::send_message(bot_token, channel_id, text);
                            }).detach();
                        });
                    } else {
                        faceit_->set_report_callback(nullptr);
                    }
                }
                if (body.has("tts_enabled")) {
                    c.tts_enabled = body["tts_enabled"].b();
                    if (tts_) tts_->set_enabled(c.tts_enabled);
                }
                c.save();

                crow::json::wvalue resp = c.to_json();
                resp["ok"] = true;
                return crow::response(resp.dump());
            });

        CROW_ROUTE(app_, "/api/twitch/auth-status")
            .methods(crow::HTTPMethod::Get)([](const crow::request&) {
                auto& s = streamsoft::twitch::auth_prompt_state();
                std::lock_guard<std::mutex> lock(s.mutex);
                crow::json::wvalue resp;
                resp["pending"] = s.pending;
                resp["verification_uri"] = s.verification_uri;
                resp["user_code"] = s.user_code;
                resp["last_result"] = s.last_result;
                resp["last_username"] = s.last_username;
                resp["last_error"] = s.last_error;
                return crow::response(resp.dump());
            });

        CROW_ROUTE(app_, "/api/twitch/start-auth")
            .methods(crow::HTTPMethod::Post)([](const crow::request& req) {
                auto body = crow::json::load(req.body);
                std::string client_id =
                    (body && body.has("client_id")) ? std::string(body["client_id"].s()) : std::string();
                if (client_id.empty()) client_id = ConnectionsConfig::load().twitch_client_id;
                if (client_id.empty()) return crow::response(400, R"({"ok": false, "error": "no client_id"})");

                {
                    auto& s = streamsoft::twitch::auth_prompt_state();
                    std::lock_guard<std::mutex> lock(s.mutex);
                    s.last_result.clear();
                    s.last_username.clear();
                    s.last_error.clear();
                }
                std::thread(streamsoft::twitch::run_manual_auth, client_id).detach();
                return crow::response(R"({"ok": true})");
            });

        // Same device-code flow as start-auth, but drops the cached token
        // first — get_access_token() normally reuses/refreshes a cached
        // token silently, so if it's stuck in a bad state (revoked, wrong
        // scopes from an old install, etc.) there was previously no way for
        // the user to force a truly fresh login from the GUI.
        CROW_ROUTE(app_, "/api/twitch/reauth")
            .methods(crow::HTTPMethod::Post)([](const crow::request& req) {
                auto body = crow::json::load(req.body);
                std::string client_id =
                    (body && body.has("client_id")) ? std::string(body["client_id"].s()) : std::string();
                if (client_id.empty()) client_id = ConnectionsConfig::load().twitch_client_id;
                if (client_id.empty()) return crow::response(400, R"({"ok": false, "error": "no client_id"})");

                streamsoft::twitch::invalidate_cached_token();
                {
                    auto& s = streamsoft::twitch::auth_prompt_state();
                    std::lock_guard<std::mutex> lock(s.mutex);
                    s.last_result.clear();
                    s.last_username.clear();
                    s.last_error.clear();
                }
                std::thread(streamsoft::twitch::run_manual_auth, client_id).detach();
                return crow::response(R"({"ok": true})");
            });
    }

    void setup_obs_routes() {
        CROW_ROUTE(app_, "/api/obs/connect")
            .methods(crow::HTTPMethod::Post)([this](const crow::request&) {
                auto result = streamsoft::obs::ensure_browser_sources_via_file(port_);

                crow::json::wvalue resp;
                resp["ok"] = result.ok;
                if (!result.ok) {
                    resp["error"] = result.error;
                } else {
                    resp["collection"] = result.collection_name;
                    auto c = ConnectionsConfig::load();
                    c.obs_connected = true;
                    c.save();
                }
                return crow::response(resp.dump());
            });

        CROW_ROUTE(app_, "/api/obs/connect-websocket")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                auto body = crow::json::load(req.body);
                std::string password = (body && body.has("password")) ? std::string(body["password"].s()) : "";
                std::string host = (body && body.has("host")) ? std::string(body["host"].s()) : "127.0.0.1";
                int port = (body && body.has("port")) ? static_cast<int>(body["port"].i()) : 4455;

                streamsoft::obs::ObsClient client;
                try {
                    client.connect(host, port, password);
                    client.ensure_browser_sources(port_);
                    client.disconnect();
                } catch (const std::exception& e) {
                    crow::json::wvalue resp;
                    resp["ok"] = false;
                    resp["error"] = e.what();
                    return crow::response(resp.dump());
                }

                crow::json::wvalue resp;
                resp["ok"] = true;
                return crow::response(resp.dump());
            });
    }

    void setup_settings_routes() {
        CROW_ROUTE(app_, "/api/settings")
            .methods(crow::HTTPMethod::Get)([this](const crow::request&) {
                return crow::response(settings_payload().dump());
            });

        CROW_ROUTE(app_, "/api/settings")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                auto body = crow::json::load(req.body);
                if (!body) return crow::response(400, R"({"ok": false, "error": "bad json"})");

                bool song_volume_touched = false;
                {
                    std::lock_guard<std::mutex> lock(runtime_mutex_);
                    if (body.has("theme")) runtime_.theme = std::string(body["theme"].s());
                    if (body.has("tts_voice_ru")) {
                        runtime_.tts_voice_ru = std::string(body["tts_voice_ru"].s());
                        if (tts_) tts_->set_voice_ru(runtime_.tts_voice_ru);
                    }
                    if (body.has("tts_voice_en")) {
                        runtime_.tts_voice_en = std::string(body["tts_voice_en"].s());
                        if (tts_) tts_->set_voice_en(runtime_.tts_voice_en);
                    }
                    if (body.has("tts_rate")) {
                        runtime_.tts_rate = std::string(body["tts_rate"].s());
                        if (tts_) tts_->set_rate(runtime_.tts_rate);
                    }
                    if (body.has("tts_volume")) {
                        runtime_.tts_volume = static_cast<int>(body["tts_volume"].i());
                        if (tts_) tts_->set_volume_percent(runtime_.tts_volume);
                    }
                    if (body.has("tts_say_author")) {
                        runtime_.tts_say_author = body["tts_say_author"].b();
                        if (tts_) tts_->set_say_author(runtime_.tts_say_author);
                    }
                    if (body.has("event_volume")) runtime_.event_volume = static_cast<int>(body["event_volume"].i());
                    if (body.has("chat_scale")) {
                        double v = body["chat_scale"].d();
                        runtime_.chat_scale = std::max(0.5, std::min(2.5, v));
                    }
                    if (body.has("alert_scale")) {
                        double v = body["alert_scale"].d();
                        runtime_.alert_scale = std::max(0.5, std::min(2.5, v));
                    }

                    bool rvc_touched = false;
                    if (body.has("rvc_enabled")) { runtime_.rvc_enabled = body["rvc_enabled"].b(); rvc_touched = true; }
                    if (body.has("rvc_model")) runtime_.rvc_model = std::string(body["rvc_model"].s());
                    if (body.has("rvc_scope")) { runtime_.rvc_scope = std::string(body["rvc_scope"].s()); rvc_touched = true; }
                    if (body.has("rvc_pitch")) { runtime_.rvc_pitch = static_cast<int>(body["rvc_pitch"].i()); rvc_touched = true; }
                    if (body.has("rvc_index_rate")) { runtime_.rvc_index_rate = body["rvc_index_rate"].d(); rvc_touched = true; }
                    if (body.has("rvc_protect")) { runtime_.rvc_protect = body["rvc_protect"].d(); rvc_touched = true; }
                    if (body.has("rvc_f0method")) { runtime_.rvc_f0method = std::string(body["rvc_f0method"].s()); rvc_touched = true; }
                    if (rvc_touched && tts_) {
                        tts_->set_rvc_settings(runtime_.rvc_enabled, runtime_.rvc_scope, runtime_.rvc_pitch,
                                                runtime_.rvc_index_rate, runtime_.rvc_protect, runtime_.rvc_f0method);
                    }

                    if (body.has("song_requests_enabled")) runtime_.song_requests_enabled = body["song_requests_enabled"].b();
                    if (body.has("song_request_cost")) runtime_.song_request_cost = static_cast<int>(body["song_request_cost"].i());
                    if (body.has("song_request_volume")) {
                        int v = static_cast<int>(body["song_request_volume"].i());
                        runtime_.song_request_volume = std::max(0, std::min(100, v));
                        song_volume_touched = true;
                    }
                    if (body.has("points_per_message")) {
                        int v = static_cast<int>(body["points_per_message"].i());
                        runtime_.points_per_message = std::max(0, v);
                    }

                    runtime_.save();
                }

                if (body.has("mute")) moderation_.mute(std::string(body["mute"].s()));
                if (body.has("unmute")) moderation_.unmute(std::string(body["unmute"].s()));

                if (song_volume_touched) broadcast_now_playing();

                broadcast_config();

                crow::json::wvalue resp = settings_payload();
                resp["ok"] = true;
                return crow::response(resp.dump());
            });
    }

    void setup_media_routes() {
        CROW_ROUTE(app_, "/api/media/status")
        ([this] {
            crow::json::wvalue status;
            for (auto kind : kEventKinds) {
                crow::json::wvalue exts;
                for (auto ext : kMediaExts) {
                    exts[ext] = std::filesystem::exists(media_dir_ / (std::string(kind) + "." + ext));
                }
                status[kind] = std::move(exts);
            }
            return crow::response(status.dump());
        });

        CROW_ROUTE(app_, "/api/media/<string>/<string>")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req, const std::string& kind, const std::string& ext) {
                if (!is_known_kind(kind) || !is_known_ext(ext)) {
                    return crow::response(400, R"({"ok": false, "error": "bad kind/ext"})");
                }
                if (req.body.empty()) {
                    return crow::response(400, R"({"ok": false, "error": "empty file"})");
                }
                std::ofstream f(media_dir_ / (kind + "." + ext), std::ios::binary | std::ios::trunc);
                f << req.body;
                return crow::response(R"({"ok": true})");
            });

        CROW_ROUTE(app_, "/api/media/<string>/<string>")
            .methods(crow::HTTPMethod::Delete)([this](const std::string& kind, const std::string& ext) {
                if (!is_known_kind(kind) || !is_known_ext(ext)) {
                    return crow::response(400, R"({"ok": false, "error": "bad kind/ext"})");
                }
                std::error_code ec;
                std::filesystem::remove(media_dir_ / (kind + "." + ext), ec);
                return crow::response(R"({"ok": true})");
            });
    }

    bool gif_file_exists(const std::string& name, const char* ext) const {
        return std::filesystem::exists(media_dir_ / ("gif_" + GifStore::normalize(name) + "." + ext));
    }

    void setup_gifs_routes() {
        CROW_ROUTE(app_, "/api/gifs")
            .methods(crow::HTTPMethod::Get)([this](const crow::request&) {
                crow::json::wvalue resp;
                std::vector<crow::json::wvalue> arr;
                for (const auto& e : gifs_.all()) {
                    crow::json::wvalue j;
                    j["name"] = e.name;
                    j["price"] = e.price;
                    j["hasGif"] = gif_file_exists(e.name, "gif");
                    j["hasMp3"] = gif_file_exists(e.name, "mp3");
                    arr.push_back(std::move(j));
                }
                resp["gifs"] = std::move(arr);
                return crow::response(resp.dump());
            });

        CROW_ROUTE(app_, "/api/gifs")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                auto body = crow::json::load(req.body);
                if (!body || !body.has("name")) return crow::response(400, R"({"ok": false, "error": "bad json"})");
                std::string name = std::string(body["name"].s());
                int price = body.has("price") ? static_cast<int>(body["price"].i()) : 50;
                gifs_.upsert(name, price);
                return crow::response(R"({"ok": true})");
            });

        CROW_ROUTE(app_, "/api/gifs/<string>")
            .methods(crow::HTTPMethod::Delete)([this](const std::string& raw_name) {
                std::string name = url_decode_segment(raw_name);
                std::error_code ec;
                std::filesystem::remove(media_dir_ / ("gif_" + GifStore::normalize(name) + ".gif"), ec);
                std::filesystem::remove(media_dir_ / ("gif_" + GifStore::normalize(name) + ".mp3"), ec);
                gifs_.remove(name);
                return crow::response(R"({"ok": true})");
            });

        CROW_ROUTE(app_, "/api/gifs/<string>/<string>")
            .methods(crow::HTTPMethod::Delete)([this](const std::string& raw_name, const std::string& ext) {
                std::string name = url_decode_segment(raw_name);
                if (!is_safe_segment(name) || !is_known_ext(ext)) {
                    return crow::response(400, R"({"ok": false, "error": "bad name/ext"})");
                }
                std::error_code ec;
                std::filesystem::remove(media_dir_ / ("gif_" + GifStore::normalize(name) + "." + ext), ec);
                return crow::response(R"({"ok": true})");
            });

        CROW_ROUTE(app_, "/api/gifs/<string>/<string>")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req, const std::string& raw_name, const std::string& ext) {
                std::string name = url_decode_segment(raw_name);
                if (!is_safe_segment(name) || !is_known_ext(ext)) {
                    return crow::response(400, R"({"ok": false, "error": "bad name/ext"})");
                }
                if (req.body.empty()) return crow::response(400, R"({"ok": false, "error": "empty file"})");
                if (!gifs_.find(name)) gifs_.upsert(name, 50);

                std::ofstream f(media_dir_ / ("gif_" + GifStore::normalize(name) + "." + ext),
                                 std::ios::binary | std::ios::trunc);
                f << req.body;
                return crow::response(R"({"ok": true})");
            });
    }

    void setup_faceit_routes() {
        CROW_ROUTE(app_, "/api/faceit/snapshot")
        ([this] {
            crow::json::wvalue resp = faceit_ ? faceit_->snapshot_json() : crow::json::wvalue();
            if (!faceit_) resp["valid"] = false;
            crow::response res(resp.dump());
            // OBS's Chromium (CEF) browser source can cache GET responses
            // across the widget's whole lifetime — without this, stat/ELO
            // changes never show up even though the widget re-fetches this
            // endpoint every 20s.
            res.set_header("Cache-Control", "no-store");
            return res;
        });
    }

    void setup_commands_routes() {
        CROW_ROUTE(app_, "/api/commands")
            .methods(crow::HTTPMethod::Get)([this](const crow::request&) {
                crow::json::wvalue resp;
                resp["commands"] = commands_.list();
                return crow::response(resp.dump());
            });

        CROW_ROUTE(app_, "/api/commands")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                auto body = crow::json::load(req.body);
                if (!body) return crow::response(400, R"({"ok": false, "error": "bad json"})");

                std::string trigger = body.has("trigger") ? std::string(body["trigger"].s()) : "";
                std::string response = body.has("response") ? std::string(body["response"].s()) : "";
                int cooldown = body.has("cooldown") ? static_cast<int>(body["cooldown"].i()) : 15;

                auto trim = [](std::string s) {
                    auto not_space = [](unsigned char c) { return !std::isspace(c); };
                    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
                    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
                    return s;
                };
                trigger = trim(trigger);
                response = trim(response);

                if (trigger.empty() || response.empty()) {
                    return crow::response(400, R"({"ok": false, "error": "trigger и response обязательны"})");
                }

                commands_.add(trigger, response, cooldown);
                crow::json::wvalue resp;
                resp["ok"] = true;
                resp["commands"] = commands_.list();
                return crow::response(resp.dump());
            });

        CROW_ROUTE(app_, "/api/commands/<string>")
            .methods(crow::HTTPMethod::Delete)([this](const std::string& trigger) {
                commands_.remove(trigger);
                crow::json::wvalue resp;
                resp["ok"] = true;
                resp["commands"] = commands_.list();
                return crow::response(resp.dump());
            });

        CROW_ROUTE(app_, "/api/commands/<string>/toggle")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req, const std::string& trigger) {
                auto body = crow::json::load(req.body);
                bool enabled = (body && body.has("enabled")) ? body["enabled"].b() : true;
                commands_.set_enabled(trigger, enabled);
                crow::json::wvalue resp;
                resp["ok"] = true;
                resp["commands"] = commands_.list();
                return crow::response(resp.dump());
            });
    }

    void setup_rvc_routes() {
        CROW_ROUTE(app_, "/api/rvc/health")
        ([this] {
            crow::json::wvalue resp;
            if (rvc_port_ == 0) {
                resp["available"] = false;
                resp["running"] = false;
                return crow::response(resp.dump());
            }
            httplib::Client cli("http://127.0.0.1:" + std::to_string(rvc_port_));
            cli.set_connection_timeout(1);
            cli.set_read_timeout(3);
            auto r = cli.Get("/health");
            if (!r || r->status != 200) {
                resp["available"] = false;
                resp["running"] = false;
                return crow::response(resp.dump());
            }
            auto data = crow::json::load(r->body);
            resp["available"] = true;
            resp["running"] = true;
            resp["device"] = data && data.has("device") ? std::string(data["device"].s()) : "";
            resp["model"] = data && data.has("model") ? std::string(data["model"].s()) : "";
            return crow::response(resp.dump());
        });

        CROW_ROUTE(app_, "/api/rvc/stop")
            .methods(crow::HTTPMethod::Post)([this] {
                if (!rvc_stop_) return crow::response(R"({"ok": false, "error": "unavailable"})");
                rvc_stop_();
                return crow::response(R"({"ok": true, "running": false})");
            });

        CROW_ROUTE(app_, "/api/rvc/start")
            .methods(crow::HTTPMethod::Post)([this] {
                if (!rvc_start_) return crow::response(R"({"ok": false, "error": "unavailable"})");
                bool running = rvc_start_();
                crow::json::wvalue resp;
                resp["ok"] = running;
                resp["running"] = running;
                if (!running) resp["error"] = "start_failed";
                return crow::response(resp.dump());
            });

        CROW_ROUTE(app_, "/api/rvc/models")
        ([this] {
            if (rvc_port_ != 0) {
                httplib::Client cli("http://127.0.0.1:" + std::to_string(rvc_port_));
                cli.set_connection_timeout(1);
                cli.set_read_timeout(3);
                auto r = cli.Get("/models");
                if (r && r->status == 200) return crow::response(r->body);
            }
            crow::json::wvalue resp;
            resp["models"] = std::vector<crow::json::wvalue>{};
            return crow::response(resp.dump());
        });
    }

    void setup_modules_routes() {
        CROW_ROUTE(app_, "/api/modules/rvc/requirements")
        ([] {
            static constexpr std::uint64_t kRvcRequiredDiskMb = 8192;

            auto gpu = streamsoft::check_gpu_for_rvc();
            auto cuda = streamsoft::detect_cuda_wheel_tag();
            auto disk = streamsoft::check_disk_space(streamsoft::rvc_module_manifest().install_dir, kRvcRequiredDiskMb);

            crow::json::wvalue resp;
            resp["cuda_capable"] = gpu.cuda_capable && cuda.available;
            resp["gpu_name"] = gpu.gpu_name;
            resp["vram_mb"] = gpu.vram_mb;
            resp["gpu_reason"] = !gpu.cuda_capable ? gpu.reason : cuda.reason;
            resp["cuda_wheel_tag"] = cuda.tag;
            resp["disk_ok"] = disk.ok;
            resp["disk_free_mb"] = disk.free_mb;
            resp["disk_required_mb"] = kRvcRequiredDiskMb;
            resp["disk_reason"] = disk.reason;
            resp["ready"] = gpu.cuda_capable && cuda.available && disk.ok;
            return crow::response(resp.dump());
        });

        CROW_ROUTE(app_, "/api/modules/<string>/status")
        ([](const std::string& name) {
            const auto* manifest = streamsoft::find_module_manifest(name);
            if (!manifest) return crow::response(404, R"({"ok": false, "error": "unknown module"})");

            crow::json::wvalue resp;
            resp["ok"] = true;
            resp["installed"] = streamsoft::is_module_installed(*manifest);
            resp["download_mb"] = manifest->total_download_mb;
            resp["requires_gpu"] = manifest->requires_gpu;

            auto& progress = streamsoft::module_progress(name);
            std::lock_guard<std::mutex> lock(progress.mutex);
            resp["state"] = streamsoft::module_state_name(progress.state);
            if (progress.state == streamsoft::ModuleInstallState::Failed) resp["error"] = progress.error;

            if (manifest->requires_gpu) {
                auto gpu = streamsoft::check_gpu_for_rvc();
                auto cuda = streamsoft::detect_cuda_wheel_tag();
                auto disk = streamsoft::check_disk_space(manifest->install_dir, manifest->required_disk_mb);
                resp["requirements_met"] = gpu.cuda_capable && cuda.available && disk.ok;
                resp["requirements_reason"] = !gpu.cuda_capable ? gpu.reason : (!cuda.available ? cuda.reason : disk.reason);
            } else {
                auto disk = streamsoft::check_disk_space(manifest->install_dir, manifest->required_disk_mb);
                resp["requirements_met"] = disk.ok;
                resp["requirements_reason"] = disk.reason;
            }
            return crow::response(resp.dump());
        });

        CROW_ROUTE(app_, "/api/modules/<string>/install")
            .methods(crow::HTTPMethod::Post)([](const std::string& name) {
                const auto* manifest = streamsoft::find_module_manifest(name);
                if (!manifest) return crow::response(404, R"({"ok": false, "error": "unknown module"})");

                if (streamsoft::is_module_installed(*manifest)) {
                    return crow::response(R"({"ok": false, "error": "already installed"})");
                }
                if (!streamsoft::install_module_async(*manifest)) {
                    return crow::response(R"({"ok": false, "error": "install already running"})");
                }
                return crow::response(R"({"ok": true})");
            });

        CROW_ROUTE(app_, "/api/modules/<string>/progress")
        ([](const std::string& name) {
            const auto* manifest = streamsoft::find_module_manifest(name);
            if (!manifest) return crow::response(404, R"({"ok": false, "error": "unknown module"})");

            auto& progress = streamsoft::module_progress(name);
            std::lock_guard<std::mutex> lock(progress.mutex);
            crow::json::wvalue resp;
            resp["ok"] = true;
            resp["state"] = streamsoft::module_state_name(progress.state);
            resp["file_index"] = progress.file_index;
            resp["file_count"] = progress.file_count;
            resp["bytes_downloaded"] = progress.bytes_downloaded;
            resp["bytes_total"] = progress.bytes_total;
            resp["current_step"] = progress.current_step;
            if (progress.state == streamsoft::ModuleInstallState::Failed) resp["error"] = progress.error;
            return crow::response(resp.dump());
        });
    }

    void setup_updates_routes() {
        CROW_ROUTE(app_, "/api/updates")
        ([] {
            crow::json::wvalue resp;
            resp["current_version"] = STREAMSOFT_VERSION;

            auto history = streamsoft::fetch_release_history();
            std::vector<crow::json::wvalue> releases;
            for (const auto& r : history) {
                crow::json::wvalue item;
                item["version"] = r.version;
                item["name"] = r.name;
                item["notes"] = r.notes;
                item["published_at"] = r.published_at;
                releases.push_back(std::move(item));
            }
            resp["releases"] = std::move(releases);
            return crow::response(resp.dump());
        });

        CROW_ROUTE(app_, "/api/log")
        ([](const crow::request& req) {
            int want_lines = 200;
            auto it = req.url_params.get("lines");
            if (it) want_lines = std::max(1, std::min(2000, std::atoi(it)));

            std::ifstream f("streamsoft.log", std::ios::binary);
            if (!f) return crow::response(404, "streamsoft.log ещё не создан");

            std::vector<std::string> lines;
            std::string line;
            while (std::getline(f, line)) {
                lines.push_back(std::move(line));
                if (static_cast<int>(lines.size()) > want_lines) lines.erase(lines.begin());
            }

            std::string body;
            for (const auto& l : lines) {
                body += l;
                body += '\n';
            }
            // The GUI's ApiClient only understands JSON responses (it parses
            // every reply as JSON and hands the caller undefined otherwise),
            // so wrap the plain-text log instead of serving it raw.
            crow::json::wvalue resp;
            resp["ok"] = true;
            resp["text"] = body;
            return crow::response(resp.dump());
        });
    }

    void setup_poll_routes() {
        CROW_ROUTE(app_, "/api/poll/start")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                auto body = crow::json::load(req.body);
                if (!body || !body.has("question") || !body.has("options")) {
                    return crow::response(400, R"({"ok": false, "error": "question/options required"})");
                }
                std::string question = std::string(body["question"].s());
                std::vector<std::string> options;
                for (const auto& o : body["options"]) options.push_back(std::string(o.s()));
                if (question.empty() || options.size() < 2 || options.size() > 9) {
                    return crow::response(400, R"({"ok": false, "error": "need 2-9 options"})");
                }
                poll_.start(question, options);
                broadcast_poll_update();

                if (twitch_outgoing_) {
                    std::string announcement = "Опрос: " + question + " — голосуй в чате: ";
                    for (size_t i = 0; i < options.size(); ++i) {
                        if (i > 0) announcement += ", ";
                        announcement += "!" + std::to_string(i + 1) + " (" + options[i] + ")";
                    }
                    twitch_outgoing_->push(announcement);
                }

                return crow::response(R"({"ok": true})");
            });

        CROW_ROUTE(app_, "/api/poll/stop")
            .methods(crow::HTTPMethod::Post)([this](const crow::request&) {
                poll_.stop();
                broadcast_poll_update();
                return crow::response(R"({"ok": true})");
            });

        CROW_ROUTE(app_, "/api/poll/status")
        ([this] {
            crow::json::wvalue resp = poll_.status();
            resp["ok"] = true;
            return crow::response(resp.dump());
        });
    }

    void setup_songqueue_routes() {
        CROW_ROUTE(app_, "/api/points")
        ([this] {
            crow::json::wvalue resp;
            resp["ok"] = true;
            resp["leaderboard"] = points_.leaderboard();
            return crow::response(resp.dump());
        });

        CROW_ROUTE(app_, "/api/songqueue/status")
        ([this] {
            crow::json::wvalue resp = song_queue_.status();
            resp["ok"] = true;
            return crow::response(resp.dump());
        });

        CROW_ROUTE(app_, "/api/songqueue/skip")
            .methods(crow::HTTPMethod::Post)([this](const crow::request&) {
                song_queue_.advance();
                broadcast_now_playing();
                return crow::response(R"({"ok": true})");
            });

        CROW_ROUTE(app_, "/api/songqueue/clear")
            .methods(crow::HTTPMethod::Post)([this](const crow::request&) {
                song_queue_.clear();
                broadcast_now_playing();
                return crow::response(R"({"ok": true})");
            });
    }

    void setup_test_routes() {
        CROW_ROUTE(app_, "/api/test-chat")
            .methods(crow::HTTPMethod::Post)([this](const crow::request&) {
                broadcast_chat("twitch", "TestUser", "Тестовое сообщение чата");
                return crow::response(R"({"ok": true})");
            });

        CROW_ROUTE(app_, "/api/test-tts")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                if (!tts_) return crow::response(503, R"({"ok": false, "error": "tts not running"})");
                auto body = crow::json::load(req.body);
                std::string text =
                    (body && body.has("text")) ? std::string(body["text"].s()) : std::string("Проверка голоса, раз, два, три");
                tts_->say("Тест", text);
                return crow::response(R"({"ok": true})");
            });

        CROW_ROUTE(app_, "/api/test-song")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                auto body = crow::json::load(req.body);
                std::string username = (body && body.has("username")) ? std::string(body["username"].s()) : "TestUser";
                std::string text = (body && body.has("text")) ? std::string(body["text"].s()) : "";
                auto reply = try_song_request(username, text);
                crow::json::wvalue resp;
                resp["ok"] = true;
                resp["consumed"] = reply.has_value();
                if (reply) resp["reply"] = *reply;
                return crow::response(resp.dump());
            });

        CROW_ROUTE(app_, "/api/test-command")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                auto body = crow::json::load(req.body);
                std::string username = (body && body.has("username")) ? std::string(body["username"].s()) : "TestUser";
                std::string text = (body && body.has("text")) ? std::string(body["text"].s()) : "";
                auto reply = try_builtin_command(username, text);
                crow::json::wvalue resp;
                resp["ok"] = true;
                resp["consumed"] = reply.has_value();
                if (reply) resp["reply"] = *reply;
                return crow::response(resp.dump());
            });

        CROW_ROUTE(app_, "/api/test-event")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                auto body = crow::json::load(req.body);
                std::string kind = (body && body.has("kind")) ? std::string(body["kind"].s()) : std::string("follow");
                broadcast_event(kind, "TestUser", "");
                return crow::response(R"({"ok": true})");
            });
    }

    void setup_ws_route() {
        CROW_WEBSOCKET_ROUTE(app_, "/ws")
            .onopen([this](crow::websocket::connection& conn) {
                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    clients_.insert(&conn);
                }
                conn.send_text(config_payload().dump());

                crow::json::wvalue poll_payload = poll_.status();
                poll_payload["type"] = "poll";
                conn.send_text(poll_payload.dump());

                conn.send_text(now_playing_payload().dump());

                std::lock_guard<std::mutex> lock(history_mutex_);
                for (const auto& msg : chat_history_) {
                    conn.send_text(msg);
                }
            })
            .onclose([this](crow::websocket::connection& conn, const std::string&, uint16_t) {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                clients_.erase(&conn);
            })
            .onmessage([this](crow::websocket::connection&, const std::string& text, bool) {
                auto msg = crow::json::load(text);
                if (msg && msg.has("type") && std::string(msg["type"].s()) == "song_ended") {
                    std::string reason = msg.has("reason") ? std::string(msg["reason"].s()) : "";
                    if (!reason.empty()) {
                        CROW_LOG_WARNING << "song_queue: track ended abnormally (" << reason << ")";
                    }
                    if (reason.rfind("yt_error_", 0) == 0 && try_youtube_fallback()) return;
                    song_queue_.advance();
                    broadcast_now_playing();
                }
            });
    }

    crow::json::wvalue config_payload() {
        std::lock_guard<std::mutex> lock(runtime_mutex_);
        crow::json::wvalue j;
        j["type"] = "config";
        j["theme"] = runtime_.theme;
        j["eventVolume"] = runtime_.event_volume / 100.0;
        j["chatScale"] = runtime_.chat_scale;
        j["alertScale"] = runtime_.alert_scale;
        return j;
    }

    crow::json::wvalue settings_payload() {
        std::lock_guard<std::mutex> lock(runtime_mutex_);
        crow::json::wvalue j = runtime_.to_json();
        j["muted"] = moderation_.list_muted();
        return j;
    }

    void broadcast_config() { broadcast_raw(config_payload().dump()); }

    crow::response serve_file(const std::filesystem::path& base, const std::string& filename) {
        std::filesystem::path full = base / filename;

        std::ifstream f(full, std::ios::binary);
        if (!f) {
            return crow::response(404);
        }

        std::ostringstream ss;
        ss << f.rdbuf();

        crow::response res(ss.str());
        res.set_header("Content-Type", content_type_for(full));
        return res;
    }

    static std::string content_type_for(const std::filesystem::path& path) {
        auto ext = path.extension().string();
        if (ext == ".html") return "text/html; charset=utf-8";
        if (ext == ".css") return "text/css";
        if (ext == ".js") return "application/javascript";
        if (ext == ".png") return "image/png";
        if (ext == ".gif") return "image/gif";
        if (ext == ".mp3") return "audio/mpeg";
        if (ext == ".mp4") return "video/mp4";
        if (ext == ".webm") return "video/webm";
        return "application/octet-stream";
    }

    void broadcast_raw(const std::string& data) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto* conn : clients_) {
            conn->send_text(data);
        }
    }

    crow::SimpleApp app_;
    int port_;
    std::filesystem::path web_dir_;
    std::filesystem::path media_dir_;

    std::mutex runtime_mutex_;
    RuntimeSettings runtime_;
    ModerationState moderation_;
    CommandsStore commands_;
    PollState poll_;
    PointsStore points_;
    SongQueue song_queue_;
    GifStore gifs_;
    faceit::FaceitClient* faceit_ = nullptr;
    std::string broadcaster_name_;
    tts::TtsWorker* tts_ = nullptr;
    int rvc_port_ = 0;
    std::function<bool()> rvc_start_;
    std::function<void()> rvc_stop_;
    OutgoingQueue* twitch_outgoing_ = nullptr;

    static constexpr int kClipCooldownSeconds = 60;
    std::string twitch_client_id_;
    std::mutex clip_mutex_;
    std::string twitch_broadcaster_id_;
    std::optional<std::chrono::steady_clock::time_point> last_clip_time_;

    std::mutex connections_mutex_;

    std::mutex clients_mutex_;
    std::set<crow::websocket::connection*> clients_;

    std::mutex history_mutex_;
    std::deque<std::string> chat_history_;
};

}
