#pragma once

#include <crow/json.h>

#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace streamsoft {

inline std::string generate_gsi_token() {
    std::random_device rd;
    std::ostringstream ss;
    for (int i = 0; i < 8; ++i) ss << std::hex << std::setw(4) << std::setfill('0') << (rd() & 0xFFFF);
    return ss.str();
}

struct RuntimeSettings {
    std::string theme = "minimal";
    std::string tts_voice_ru = "ru-RU-DmitryNeural";
    std::string tts_voice_en = "en-US-GuyNeural";
    std::string tts_rate = "+0%";
    int tts_volume = 100;
    bool tts_say_author = true;
    int event_volume = 100;
    double chat_scale = 1.0;
    double alert_scale = 1.0;

    bool rvc_enabled = false;
    std::string rvc_base_url = "http://127.0.0.1:8100";
    std::string rvc_model = "ayaka";
    std::string rvc_scope = "alerts";
    int rvc_pitch = 12;
    double rvc_index_rate = 0.3;
    double rvc_protect = 0.5;
    std::string rvc_f0method = "rmvpe";

    bool song_requests_enabled = false;
    int song_request_cost = 50;
    int song_request_volume = 80;
    int points_per_message = 1;

    bool bets_enabled = false;
    int bet_min = 10;
    int bet_max = 500;
    double bet_payout_multiplier = 2.0;
    std::string gsi_token;

    static constexpr const char* kFile = "runtime_settings.json";

    static RuntimeSettings load() {
        RuntimeSettings s;

        std::ifstream f(kFile, std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            auto j = crow::json::load(ss.str());
            if (j) {
                if (j.has("theme")) s.theme = std::string(j["theme"].s());
                if (j.has("tts_voice_ru")) s.tts_voice_ru = std::string(j["tts_voice_ru"].s());
                if (j.has("tts_voice_en")) s.tts_voice_en = std::string(j["tts_voice_en"].s());
                if (j.has("tts_rate")) s.tts_rate = std::string(j["tts_rate"].s());
                if (j.has("tts_volume")) s.tts_volume = static_cast<int>(j["tts_volume"].i());
                if (j.has("tts_say_author")) s.tts_say_author = j["tts_say_author"].b();
                if (j.has("event_volume")) s.event_volume = static_cast<int>(j["event_volume"].i());
                if (j.has("chat_scale")) s.chat_scale = j["chat_scale"].d();
                if (j.has("alert_scale")) s.alert_scale = j["alert_scale"].d();

                if (j.has("rvc_enabled")) s.rvc_enabled = j["rvc_enabled"].b();
                if (j.has("rvc_base_url")) s.rvc_base_url = std::string(j["rvc_base_url"].s());
                if (j.has("rvc_model")) s.rvc_model = std::string(j["rvc_model"].s());
                if (j.has("rvc_scope")) s.rvc_scope = std::string(j["rvc_scope"].s());
                if (j.has("rvc_pitch")) s.rvc_pitch = static_cast<int>(j["rvc_pitch"].i());
                if (j.has("rvc_index_rate")) s.rvc_index_rate = j["rvc_index_rate"].d();
                if (j.has("rvc_protect")) s.rvc_protect = j["rvc_protect"].d();
                if (j.has("rvc_f0method")) s.rvc_f0method = std::string(j["rvc_f0method"].s());

                if (j.has("song_requests_enabled")) s.song_requests_enabled = j["song_requests_enabled"].b();
                if (j.has("song_request_cost")) s.song_request_cost = static_cast<int>(j["song_request_cost"].i());
                if (j.has("song_request_volume")) s.song_request_volume = static_cast<int>(j["song_request_volume"].i());
                if (j.has("points_per_message")) s.points_per_message = static_cast<int>(j["points_per_message"].i());

                if (j.has("bets_enabled")) s.bets_enabled = j["bets_enabled"].b();
                if (j.has("bet_min")) s.bet_min = static_cast<int>(j["bet_min"].i());
                if (j.has("bet_max")) s.bet_max = static_cast<int>(j["bet_max"].i());
                if (j.has("bet_payout_multiplier")) s.bet_payout_multiplier = j["bet_payout_multiplier"].d();
                if (j.has("gsi_token")) s.gsi_token = std::string(j["gsi_token"].s());
            }
        }

        if (s.gsi_token.empty()) {
            s.gsi_token = generate_gsi_token();
            s.save();
        }

        return s;
    }

    crow::json::wvalue to_json() const {
        crow::json::wvalue j;
        j["theme"] = theme;
        j["tts_voice_ru"] = tts_voice_ru;
        j["tts_voice_en"] = tts_voice_en;
        j["tts_rate"] = tts_rate;
        j["tts_volume"] = tts_volume;
        j["tts_say_author"] = tts_say_author;
        j["event_volume"] = event_volume;
        j["chat_scale"] = chat_scale;
        j["alert_scale"] = alert_scale;

        j["rvc_enabled"] = rvc_enabled;
        j["rvc_base_url"] = rvc_base_url;
        j["rvc_model"] = rvc_model;
        j["rvc_scope"] = rvc_scope;
        j["rvc_pitch"] = rvc_pitch;
        j["rvc_index_rate"] = rvc_index_rate;
        j["rvc_protect"] = rvc_protect;
        j["rvc_f0method"] = rvc_f0method;

        j["song_requests_enabled"] = song_requests_enabled;
        j["song_request_cost"] = song_request_cost;
        j["song_request_volume"] = song_request_volume;
        j["points_per_message"] = points_per_message;

        j["bets_enabled"] = bets_enabled;
        j["bet_min"] = bet_min;
        j["bet_max"] = bet_max;
        j["bet_payout_multiplier"] = bet_payout_multiplier;
        j["gsi_token"] = gsi_token;
        return j;
    }

    void save() const {
        std::ofstream f(kFile, std::ios::binary | std::ios::trunc);
        f << to_json().dump();
    }
};

}
