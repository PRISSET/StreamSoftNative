#pragma once

#include "env_config.hpp"

#include <crow/json.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

namespace streamsoft {

// Twitch's Helix API rejects a login with a leading "@", surrounding
// whitespace, or a pasted "twitch.tv/<name>" URL outright ("Bad
// Identifiers") — normalize whatever the user typed instead of silently
// failing every EventSub/user-id lookup with that channel.
inline std::string normalize_twitch_channel(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());

    auto strip_prefix = [&](const std::string& prefix) {
        if (s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin(),
                                                      [](char a, char b) { return ::tolower(a) == ::tolower(b); })) {
            s.erase(0, prefix.size());
        }
    };
    strip_prefix("https://www.twitch.tv/");
    strip_prefix("https://twitch.tv/");
    strip_prefix("www.twitch.tv/");
    strip_prefix("twitch.tv/");
    if (!s.empty() && s.front() == '@') s.erase(0, 1);

    size_t slash = s.find('/');
    if (slash != std::string::npos) s.erase(slash);

    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

struct ConnectionsConfig {
    std::string twitch_client_id;
    std::string twitch_channel;
    bool twitch_chat_enabled = true;
    bool twitch_eventsub_enabled = true;

    std::string youtube_api_key;
    std::string youtube_video_id;
    std::string youtube_channel_id;
    bool youtube_enabled = true;

    // Separate from youtube_api_key — that one is a plain API key, only
    // good for read-only calls (chat). Updating a live broadcast's title
    // needs a real user OAuth token, hence its own client id/secret pair
    // (see youtube_auth.hpp).
    std::string youtube_oauth_client_id;
    std::string youtube_oauth_client_secret;

    std::string telegram_bot_token;
    std::string telegram_chat_id;
    bool telegram_enabled = false;
    bool telegram_control_enabled = false;

    std::string social_telegram_channel_id;
    bool social_telegram_enabled = false;

    std::string faceit_nickname;
    std::string faceit_api_key;
    bool faceit_enabled = false;
    bool faceit_stats_telegram_enabled = false;

    std::string dota_account_id;
    bool dota_enabled = false;

    std::string multistream_label = "Вторая площадка";
    std::string multistream_server;
    std::string multistream_key;
    bool multistream_enabled = false;

    std::string stream_title;

    bool tts_enabled = true;
    int tts_max_chars = 200;

    bool obs_connected = false;

    static constexpr const char* kFile = "connections.json";

    bool has_twitch() const { return !twitch_client_id.empty() && !twitch_channel.empty(); }
    // A fixed video id keeps working for a single broadcast the way it
    // always has; a channel id lets watch_youtube() (youtube_chat.hpp)
    // resolve whichever broadcast is currently live on its own, so nobody
    // has to paste in a new video id before every single stream.
    bool has_youtube() const {
        return !youtube_api_key.empty() && (!youtube_video_id.empty() || !youtube_channel_id.empty());
    }
    bool has_youtube_oauth() const { return !youtube_oauth_client_id.empty() && !youtube_oauth_client_secret.empty(); }
    bool has_telegram() const { return !telegram_bot_token.empty() && !telegram_chat_id.empty(); }
    bool has_social_telegram() const { return !telegram_bot_token.empty() && !social_telegram_channel_id.empty(); }
    // Faceit works out of the box on a shared API key (see faceit_shared_key.hpp)
    // — a user's own key is optional, only needed if the shared one ever
    // gets rate-limited.
    bool has_faceit() const { return !faceit_nickname.empty(); }
    bool has_dota() const { return !dota_account_id.empty(); }
    bool has_multistream() const { return !multistream_server.empty() && !multistream_key.empty(); }
    // Reuses the same Telegram channel/bot as "stream started" posts (see
    // SocialPage) — a dedicated channel field would just duplicate config
    // the user already filled in.
    bool should_post_faceit_stats() const {
        return faceit_stats_telegram_enabled && has_social_telegram() && has_faceit();
    }

    bool should_run_twitch_chat() const { return twitch_chat_enabled && has_twitch(); }
    bool should_run_twitch_eventsub() const { return twitch_eventsub_enabled && has_twitch(); }
    bool should_run_youtube() const { return youtube_enabled && has_youtube(); }
    bool should_run_telegram() const { return telegram_enabled && has_telegram(); }
    bool should_run_telegram_control() const { return should_run_telegram() && telegram_control_enabled; }
    bool should_post_social_telegram() const { return social_telegram_enabled && has_social_telegram(); }
    bool should_run_faceit() const { return faceit_enabled && has_faceit(); }
    bool should_run_dota() const { return dota_enabled && has_dota(); }
    bool should_run_multistream() const { return multistream_enabled && has_multistream(); }

    static ConnectionsConfig load() {
        std::ifstream f(kFile, std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            auto j = crow::json::load(ss.str());
            if (j) {
                ConnectionsConfig c;
                auto str = [&](const char* key, std::string& out) {
                    if (j.has(key)) out = std::string(j[key].s());
                };
                auto boolean = [&](const char* key, bool& out) {
                    if (j.has(key)) out = j[key].b();
                };
                auto integer = [&](const char* key, int& out) {
                    if (j.has(key)) out = static_cast<int>(j[key].i());
                };
                str("twitch_client_id", c.twitch_client_id);
                str("twitch_channel", c.twitch_channel);
                c.twitch_channel = normalize_twitch_channel(c.twitch_channel);
                boolean("twitch_chat_enabled", c.twitch_chat_enabled);
                boolean("twitch_eventsub_enabled", c.twitch_eventsub_enabled);
                str("youtube_api_key", c.youtube_api_key);
                str("youtube_video_id", c.youtube_video_id);
                str("youtube_channel_id", c.youtube_channel_id);
                str("youtube_oauth_client_id", c.youtube_oauth_client_id);
                str("youtube_oauth_client_secret", c.youtube_oauth_client_secret);
                boolean("youtube_enabled", c.youtube_enabled);
                str("telegram_bot_token", c.telegram_bot_token);
                str("telegram_chat_id", c.telegram_chat_id);
                boolean("telegram_enabled", c.telegram_enabled);
                boolean("telegram_control_enabled", c.telegram_control_enabled);
                str("social_telegram_channel_id", c.social_telegram_channel_id);
                boolean("social_telegram_enabled", c.social_telegram_enabled);
                str("faceit_nickname", c.faceit_nickname);
                str("faceit_api_key", c.faceit_api_key);
                boolean("faceit_enabled", c.faceit_enabled);
                boolean("faceit_stats_telegram_enabled", c.faceit_stats_telegram_enabled);
                str("dota_account_id", c.dota_account_id);
                boolean("dota_enabled", c.dota_enabled);
                str("multistream_label", c.multistream_label);
                str("multistream_server", c.multistream_server);
                str("multistream_key", c.multistream_key);
                boolean("multistream_enabled", c.multistream_enabled);
                str("stream_title", c.stream_title);
                boolean("tts_enabled", c.tts_enabled);
                integer("tts_max_chars", c.tts_max_chars);
                boolean("obs_connected", c.obs_connected);
                return c;
            }
        }

        ConnectionsConfig c;
        Config env = load_config();
        c.twitch_client_id = env.twitch_client_id;
        c.twitch_channel = normalize_twitch_channel(env.twitch_channel);
        c.twitch_eventsub_enabled = env.twitch_eventsub_enabled;
        c.youtube_api_key = env.youtube_api_key;
        c.youtube_video_id = env.youtube_video_id;
        c.tts_max_chars = env.tts_max_chars;
        c.telegram_bot_token = streamsoft::env("TELEGRAM_BOT_TOKEN");
        c.telegram_chat_id = streamsoft::env("TELEGRAM_CHAT_ID");
        c.telegram_enabled = c.has_telegram();
        c.telegram_control_enabled = streamsoft::env_bool("TELEGRAM_CONTROL_ENABLED", true);
        c.save();
        return c;
    }

    crow::json::wvalue to_json() const {
        crow::json::wvalue j;
        j["twitch_client_id"] = twitch_client_id;
        j["twitch_channel"] = twitch_channel;
        j["twitch_chat_enabled"] = twitch_chat_enabled;
        j["twitch_eventsub_enabled"] = twitch_eventsub_enabled;
        j["youtube_api_key"] = youtube_api_key;
        j["youtube_video_id"] = youtube_video_id;
        j["youtube_channel_id"] = youtube_channel_id;
        j["youtube_oauth_client_id"] = youtube_oauth_client_id;
        j["youtube_oauth_client_secret"] = youtube_oauth_client_secret;
        j["youtube_enabled"] = youtube_enabled;
        j["telegram_bot_token"] = telegram_bot_token;
        j["telegram_chat_id"] = telegram_chat_id;
        j["telegram_enabled"] = telegram_enabled;
        j["telegram_control_enabled"] = telegram_control_enabled;
        j["social_telegram_channel_id"] = social_telegram_channel_id;
        j["social_telegram_enabled"] = social_telegram_enabled;
        j["faceit_nickname"] = faceit_nickname;
        j["faceit_api_key"] = faceit_api_key;
        j["faceit_enabled"] = faceit_enabled;
        j["faceit_stats_telegram_enabled"] = faceit_stats_telegram_enabled;
        j["dota_account_id"] = dota_account_id;
        j["dota_enabled"] = dota_enabled;
        j["multistream_label"] = multistream_label;
        j["multistream_server"] = multistream_server;
        j["multistream_key"] = multistream_key;
        j["multistream_enabled"] = multistream_enabled;
        j["stream_title"] = stream_title;
        j["tts_enabled"] = tts_enabled;
        j["tts_max_chars"] = tts_max_chars;
        j["obs_connected"] = obs_connected;
        return j;
    }

    void save() const {
        std::ofstream f(kFile, std::ios::binary | std::ios::trunc);
        f << to_json().dump();
    }
};

}
