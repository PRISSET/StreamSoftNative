#pragma once

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <regex>
#include <string>

namespace streamsoft {

// Loads KEY=VALUE pairs from a .env file into the process environment,
// without overriding variables already set (same semantics as python-dotenv,
// which softforstream/config.py relies on) — env vars set by the shell/OS
// always win over the file.
inline void load_dotenv(const std::string& path = ".env") {
    std::ifstream f(path);
    if (!f) return;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            const char* ws = " \t\r\n";
            s.erase(0, s.find_first_not_of(ws));
            auto last = s.find_last_not_of(ws);
            if (last != std::string::npos) s.erase(last + 1);
        };
        trim(key);
        trim(value);
        if (key.empty()) continue;

        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                   (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        if (std::getenv(key.c_str()) == nullptr) {
            _putenv_s(key.c_str(), value.c_str());
        }
    }
}

inline std::string env(const std::string& name, const std::string& default_value = "") {
    const char* v = std::getenv(name.c_str());
    if (!v) return default_value;
    std::string s(v);
    // trim
    const char* ws = " \t\r\n";
    s.erase(0, s.find_first_not_of(ws));
    auto last = s.find_last_not_of(ws);
    if (last != std::string::npos) s.erase(last + 1);
    return s;
}

inline bool env_bool(const std::string& name, bool default_value) {
    std::string v = env(name);
    if (v.empty()) return default_value;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    return v == "true" || v == "1" || v == "yes" || v == "on";
}

// Accepts either a bare 11-char YouTube video ID or a full URL
// (watch?v=, youtu.be/, /live/) — mirrors config.py's _extract_youtube_id.
inline std::string extract_youtube_id(const std::string& value) {
    if (value.empty()) return value;
    static const std::regex re(R"((?:v=|youtu\.be/|/live/)([A-Za-z0-9_-]{11}))");
    std::smatch m;
    if (std::regex_search(value, m, re)) {
        return m[1].str();
    }
    return value;
}

struct Config {
    std::string twitch_client_id;
    std::string twitch_channel;
    bool twitch_eventsub_enabled = true;

    std::string youtube_api_key;
    std::string youtube_video_id;

    int tts_max_chars = 200;

    bool has_twitch() const { return !twitch_client_id.empty() && !twitch_channel.empty(); }
    bool has_youtube() const { return !youtube_api_key.empty() && !youtube_video_id.empty(); }
};

inline std::string lstrip_hash(std::string s) {
    size_t i = 0;
    while (i < s.size() && s[i] == '#') ++i;
    return s.substr(i);
}

inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

inline Config load_config() {
    load_dotenv();

    Config cfg;
    cfg.twitch_client_id = env("TWITCH_CLIENT_ID");
    cfg.twitch_channel = to_lower(lstrip_hash(env("TWITCH_CHANNEL")));
    cfg.twitch_eventsub_enabled = env_bool("TWITCH_EVENTSUB_ENABLED", true);

    cfg.youtube_api_key = env("YOUTUBE_API_KEY");
    cfg.youtube_video_id = extract_youtube_id(env("YOUTUBE_VIDEO_ID"));

    std::string max_chars = env("TTS_MAX_CHARS");
    if (!max_chars.empty()) cfg.tts_max_chars = std::atoi(max_chars.c_str());

    return cfg;
}

} // namespace streamsoft
