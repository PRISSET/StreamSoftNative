#pragma once

#include "env_config.hpp"

#include <crow/json.h>

#include <fstream>
#include <sstream>
#include <string>

namespace streamsoft {

struct ConnectionsConfig {
    std::string twitch_client_id;
    std::string twitch_channel;
    bool twitch_chat_enabled = true;
    bool twitch_eventsub_enabled = true;

    std::string youtube_api_key;
    std::string youtube_video_id;
    bool youtube_enabled = true;

    std::string telegram_bot_token;
    std::string telegram_chat_id;
    bool telegram_enabled = false;
    bool telegram_control_enabled = false;

    bool tts_enabled = true;
    int tts_max_chars = 200;

    bool obs_connected = false;

    static constexpr const char* kFile = "connections.json";

    bool has_twitch() const { return !twitch_client_id.empty() && !twitch_channel.empty(); }
    bool has_youtube() const { return !youtube_api_key.empty() && !youtube_video_id.empty(); }
    bool has_telegram() const { return !telegram_bot_token.empty() && !telegram_chat_id.empty(); }

    bool should_run_twitch_chat() const { return twitch_chat_enabled && has_twitch(); }
    bool should_run_twitch_eventsub() const { return twitch_eventsub_enabled && has_twitch(); }
    bool should_run_youtube() const { return youtube_enabled && has_youtube(); }
    bool should_run_telegram() const { return telegram_enabled && has_telegram(); }
    bool should_run_telegram_control() const { return should_run_telegram() && telegram_control_enabled; }

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
                boolean("twitch_chat_enabled", c.twitch_chat_enabled);
                boolean("twitch_eventsub_enabled", c.twitch_eventsub_enabled);
                str("youtube_api_key", c.youtube_api_key);
                str("youtube_video_id", c.youtube_video_id);
                boolean("youtube_enabled", c.youtube_enabled);
                str("telegram_bot_token", c.telegram_bot_token);
                str("telegram_chat_id", c.telegram_chat_id);
                boolean("telegram_enabled", c.telegram_enabled);
                boolean("telegram_control_enabled", c.telegram_control_enabled);
                boolean("tts_enabled", c.tts_enabled);
                integer("tts_max_chars", c.tts_max_chars);
                boolean("obs_connected", c.obs_connected);
                return c;
            }
        }

        ConnectionsConfig c;
        Config env = load_config();
        c.twitch_client_id = env.twitch_client_id;
        c.twitch_channel = env.twitch_channel;
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
        j["youtube_enabled"] = youtube_enabled;
        j["telegram_bot_token"] = telegram_bot_token;
        j["telegram_chat_id"] = telegram_chat_id;
        j["telegram_enabled"] = telegram_enabled;
        j["telegram_control_enabled"] = telegram_control_enabled;
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
