#pragma once

#include <crow.h>
#include <httplib.h>

#include <array>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include "auto_update.hpp"
#include "chat_commands.hpp"
#include "connections_config.hpp"
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
#include "tts_worker.hpp"
#include "twitch_auth.hpp"

namespace streamsoft {

// Serves the OBS-facing overlay pages (/, /chat, /events) and static assets,
// broadcasts chat/event JSON to every connected browser source over /ws, and
// exposes the same REST API surface as softforstream/overlay_server.py
// (/api/settings, /api/media/*, /api/commands/*, /api/rvc/* stub) — this is
// the contract the Qt settings GUI (gui/) talks to, same as the Python
// settings.html/js did against the Python server.
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

    // Called from the Twitch/YouTube chat callbacks *before* the normal
    // broadcast_chat/TTS path — a successful vote is consumed silently
    // (see poll.hpp's try_vote() comment) instead of showing up as a
    // regular chat line.
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

    // One point per qualifying chat message — called from the normal
    // (non-vote, non-song-request) chat path in core_app.hpp.
    void award_points_for_message(const std::string& username) { points_.award_for_message(username); }

    // Set once at startup from ConnectionsConfig::twitch_channel — the
    // streamer's own messages come through the exact same IRC feed as
    // everyone else's (Twitch echoes your own chat back to you), so without
    // this the broadcaster would need to farm their own points before their
    // own "!song" works. Case-insensitive since Twitch usernames are.
    void set_broadcaster_name(const std::string& name) { broadcaster_name_ = name; }

    // Set once from core_app.hpp after the queue is constructed — lets
    // try_builtin_command()'s "!help"/"!points" replies and the periodic
    // points/song reminder (see song_reminder_text()) actually reach Twitch
    // chat the same way try_song_request()'s replies do. YouTube has no
    // outgoing channel at all (read-only worker), so these two features are
    // Twitch-only until that changes.
    void set_twitch_outgoing(OutgoingQueue* queue) { twitch_outgoing_ = queue; }
    OutgoingQueue* twitch_outgoing() const { return twitch_outgoing_; }

    // Small always-on informational commands, checked before !song/chat
    // commands — "!help" so viewers can discover !song/!points/poll voting
    // without having to be told, "!points" so they can check their balance
    // without waiting for the periodic reminder. std::nullopt means it
    // wasn't one of these, so normal chat handling should proceed.
    std::optional<std::string> try_builtin_command(const std::string& username, const std::string& text) {
        std::string lower = trim(text);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "!points" || lower == "!баллы") {
            return username + ", у тебя " + std::to_string(points_.balance(username)) + " баллов";
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
            return help;
        }

        return std::nullopt;
    }

    // Called from a background timer in core_app.hpp — nullopt means "don't
    // send anything this round" (feature currently off), so the reminder
    // just quietly skips instead of advertising something disabled. Kept to
    // a long interval by the caller specifically so this never reads as spam.
    std::optional<std::string> song_reminder_text() {
        std::lock_guard<std::mutex> lock(runtime_mutex_);
        if (!runtime_.song_requests_enabled) return std::nullopt;
        return "Пиши в чат — получаешь баллы за активность! На них можно заказать музыку: !song <ссылка "
               "YouTube/SoundCloud> (" +
               std::to_string(runtime_.song_request_cost) + " баллов). Проверить баланс — !points";
    }

    // "!song <link>" — returns a reply string on any outcome (added to
    // queue, rejected for a bad link, rejected for insufficient points) so
    // callers can push it back to Twitch chat the same way match_command()
    // replies do; std::nullopt means the message wasn't a song request at
    // all (feature off, or doesn't start with "!song "), so normal chat
    // handling should proceed as usual.
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

    void broadcast_now_playing() { broadcast_raw(now_playing_payload().dump()); }

    // Shared by the broadcast path and the WS onopen greeting so a freshly
    // connecting /nowplaying page and a live one always agree on volume —
    // song_queue_.status() alone doesn't know about runtime_ settings.
    crow::json::wvalue now_playing_payload() {
        auto payload = song_queue_.status();
        payload["type"] = "now_playing";
        std::lock_guard<std::mutex> lock(runtime_mutex_);
        payload["volume"] = runtime_.song_request_volume;
        return payload;
    }

    // Set once from main() after the TTS worker exists — lets live settings
    // changes from the GUI (voice/rate/say_author) apply immediately, same
    // as overlay_server.py calling self._tts.set_voice_ru() etc. inline.
    void set_tts_worker(tts::TtsWorker* tts) { tts_ = tts; }

    // Set once at startup (and again whenever the RVC adapter gets launched
    // after a fresh Check&Install, see core_app.hpp) — 0 means "adapter not
    // running", in which case /api/rvc/health degrades to available:false
    // instead of trying to reach a nonexistent port.
    void set_rvc_port(int port) { rvc_port_ = port; }

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

    // Route params come in as a single path segment (no '/'), but a segment
    // can still literally be ".." — reject anything that isn't a plain
    // filename before touching the filesystem.
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

        // Crow runs multithreaded — without this lock, two POSTs landing
        // close together (e.g. tabbing from the Client ID field straight to
        // the channel field, each firing its own save()) could both
        // load() the file before either save()s, and whichever writes last
        // would silently overwrite the other's change. This is the "Client
        // ID sometimes doesn't save" bug: nothing wrong with the field
        // itself, just two save-the-whole-file requests racing.
        CROW_ROUTE(app_, "/api/connections")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                auto body = crow::json::load(req.body);
                if (!body) return crow::response(400, R"({"ok": false, "error": "bad json"})");

                std::lock_guard<std::mutex> lock(connections_mutex_);
                auto c = ConnectionsConfig::load();
                if (body.has("twitch_client_id")) c.twitch_client_id = std::string(body["twitch_client_id"].s());
                if (body.has("twitch_channel")) c.twitch_channel = std::string(body["twitch_channel"].s());
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
                if (body.has("tts_enabled")) {
                    c.tts_enabled = body["tts_enabled"].b();
                    if (tts_) tts_->set_enabled(c.tts_enabled);
                }
                c.save();

                crow::json::wvalue resp = c.to_json();
                resp["ok"] = true;
                return crow::response(resp.dump());
            });

        // Polled by the GUI (ConnectionsPage.qml / Main.qml banner) every
        // few seconds — see ScopedAuthPrompt in twitch_auth.hpp for why
        // this exists at all: the device-code flow's verification_uri and
        // user_code have nowhere else to surface in a windowed GUI build.
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

        // Fires the device-code flow immediately after the GUI saves a
        // Client ID, instead of waiting for the Twitch worker threads
        // (which only start once, at the next full app launch) to get to
        // it — see run_manual_auth() in twitch_auth.hpp.
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
    }

    void setup_obs_routes() {
        // File-based by default: edits OBS's own scene collection JSON
        // directly (see obs_scene_file.hpp) instead of going through
        // obs-websocket — nobody's going to find "Tools -> WebSocket Server
        // Settings -> Enable" on their own, and OBS 28+ ships that server
        // *off* by default. The trade-off is OBS has to be closed for this
        // to work (editing config out from under a running instance risks
        // corrupting it), which the handler checks and refuses otherwise.
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

        // obs-websocket path kept as a fallback for anyone who *has*
        // enabled it (e.g. it lets sources update while OBS stays open) —
        // not used by the GUI's primary button, but reachable directly.
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
                    // Without this, toggling RVC on/off or dragging its
                    // sliders saved to runtime_settings.json and reflected
                    // back in the UI just fine, but speak() never found out
                    // — confirmed live: adapter healthy, toggle "on", still
                    // only ever played the plain TTS voice.
                    if (rvc_touched && tts_) {
                        tts_->set_rvc_settings(runtime_.rvc_enabled, runtime_.rvc_scope, runtime_.rvc_pitch,
                                                runtime_.rvc_index_rate, runtime_.rvc_protect, runtime_.rvc_f0method);
                    }

                    bool ducking_touched = false;
                    if (body.has("ducking_enabled")) { runtime_.ducking_enabled = body["ducking_enabled"].b(); ducking_touched = true; }
                    if (body.has("ducking_percent")) { runtime_.ducking_percent = static_cast<int>(body["ducking_percent"].i()); ducking_touched = true; }
                    if (ducking_touched && tts_) tts_->set_ducking(runtime_.ducking_enabled, runtime_.ducking_percent);

                    if (body.has("song_requests_enabled")) runtime_.song_requests_enabled = body["song_requests_enabled"].b();
                    if (body.has("song_request_cost")) runtime_.song_request_cost = static_cast<int>(body["song_request_cost"].i());
                    if (body.has("song_request_volume")) {
                        int v = static_cast<int>(body["song_request_volume"].i());
                        runtime_.song_request_volume = std::max(0, std::min(100, v));
                        song_volume_touched = true;
                    }

                    runtime_.save();
                }

                if (body.has("mute")) moderation_.mute(std::string(body["mute"].s()));
                if (body.has("unmute")) moderation_.unmute(std::string(body["unmute"].s()));

                // Lets a volume change apply to whatever's already playing in
                // /nowplaying immediately, instead of waiting for the next
                // track to pick up the new value.
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

                // trim
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
        // Real proxy to the adapter core launched itself (see
        // rvc_launcher.hpp / core_app.hpp) — degrades to available:false if
        // the adapter isn't installed/running yet, same graceful-degrade
        // contract as TTS (CLAUDE.md §4), instead of failing the whole app.
        CROW_ROUTE(app_, "/api/rvc/health")
        ([this] {
            crow::json::wvalue resp;
            if (rvc_port_ == 0) {
                resp["available"] = false;
                return crow::response(resp.dump());
            }
            httplib::Client cli("http://127.0.0.1:" + std::to_string(rvc_port_));
            cli.set_connection_timeout(1);
            cli.set_read_timeout(3);
            auto r = cli.Get("/health");
            if (!r || r->status != 200) {
                resp["available"] = false;
                return crow::response(resp.dump());
            }
            auto data = crow::json::load(r->body);
            resp["available"] = true;
            resp["device"] = data && data.has("device") ? std::string(data["device"].s()) : "";
            resp["model"] = data && data.has("model") ? std::string(data["model"].s()) : "";
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
        // Pre-install requirements check for the optional RVC module (see
        // CLAUDE.md §2 "Check & Install") — used by RvcPage.qml before it
        // ever offers a download button. 8GB matches the ~6.6GB real
        // package (venv+CUDA torch+models) plus headroom.
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

        // GET .../status: what the GUI polls on page load to decide which of
        // the four install-card states to show (see module_installer.hpp's
        // ModuleInstallState + CLAUDE.md §2's "Check & Install" steps).
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

    // Powers the GUI's "Обновления" page — same GitHub Releases data
    // auto_update.hpp already checks in the background, just surfaced for
    // the user to read instead of acted on silently.
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

                // Viewers only know !1/!2 vote syntax if we tell them —
                // announced once here rather than repeated per-vote, so it
                // doesn't turn into chat noise while the poll is running.
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

        // Exercises the real TtsWorker::speak() path (including the RVC
        // conversion step, if enabled) without needing a live Twitch/YouTube
        // connection — same idea as /api/test-chat above, just for TTS.
        CROW_ROUTE(app_, "/api/test-tts")
            .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
                if (!tts_) return crow::response(503, R"({"ok": false, "error": "tts not running"})");
                auto body = crow::json::load(req.body);
                std::string text =
                    (body && body.has("text")) ? std::string(body["text"].s()) : std::string("Проверка голоса, раз, два, три");
                tts_->say("Тест", text);
                return crow::response(R"({"ok": true})");
            });

        // Exercises try_song_request() end-to-end (points spend + link
        // parsing + queue) the same way a real chat message would, without
        // needing a live Twitch/YouTube connection to trigger it.
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

        // Exercises try_builtin_command() (!help, !points) without needing a
        // live chat connection — mirrors /api/test-song. Deliberately never
        // touches twitch_outgoing_, so this can't post to real chat even
        // while a real Twitch connection is live.
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
                // The only overlay page that actually sends anything back:
                // nowplaying.html's YouTube/SoundCloud embed players report
                // "song_ended" here when a track finishes, so the queue can
                // advance — everything else just receives.
                auto msg = crow::json::load(text);
                if (msg && msg.has("type") && std::string(msg["type"].s()) == "song_ended") {
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
    std::string broadcaster_name_;
    tts::TtsWorker* tts_ = nullptr;
    int rvc_port_ = 0;
    OutgoingQueue* twitch_outgoing_ = nullptr;

    std::mutex connections_mutex_;

    std::mutex clients_mutex_;
    std::set<crow::websocket::connection*> clients_;

    std::mutex history_mutex_;
    std::deque<std::string> chat_history_;
};

} // namespace streamsoft
