#pragma once

#include "app_paths.hpp"

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string>
#include <thread>

#include "moderation.hpp"
#include "tts_worker.hpp"

namespace streamsoft::telegram {

inline httplib::Client make_client() {
    httplib::Client cli("https://api.telegram.org");
    cli.set_ca_cert_path(resolve_resource_file("certs/cacert.pem", STREAMSOFT_CACERT_PATH).c_str());
    cli.enable_server_certificate_verification(true);
    cli.set_connection_timeout(10);
    return cli;
}

inline void send_message(const std::string& bot_token, const std::string& chat_id, const std::string& text) {
    auto cli = make_client();
    httplib::Params params{{"chat_id", chat_id}, {"text", text}};
    auto resp = cli.Post("/bot" + bot_token + "/sendMessage", params);
    if (!resp || resp->status != 200) {
        CROW_LOG_ERROR << "Telegram API ошибка: " << (resp ? std::to_string(resp->status) : "no response");
    }
}

inline std::string platform_label(const std::string& platform) {
    if (platform == "youtube") return "\xF0\x9F\x94\xB4 YouTube";
    if (platform == "twitch") return "\xF0\x9F\x92\x9C Twitch";
    return platform;
}

inline std::string event_label(const std::string& kind) {
    if (kind == "follow") return "\xF0\x9F\x92\x9A \xD0\x9D\xD0\xBE\xD0\xB2\xD1\x8B\xD0\xB9 \xD1\x84\xD0\xBE\xD0\xBB\xD0\xBB\xD0\xBE\xD1\x83";
    if (kind == "subscribe")
        return "\xE2\xAD\x90 \xD0\x9D\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x8F \xD0\xBF\xD0\xBE\xD0\xB4\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBA\xD0\xB0";
    if (kind == "gift_sub")
        return "\xF0\x9F\x8E\x81 \xD0\x9F\xD0\xBE\xD0\xB4\xD0\xB0\xD1\x80\xD0\xBE\xD1\x87\xD0\xBD\xD0\xB0\xD1\x8F "
               "\xD0\xBF\xD0\xBE\xD0\xB4\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBA\xD0\xB0";
    if (kind == "raid") return "\xF0\x9F\x9A\x80 \xD0\xA0\xD0\xB5\xD0\xB9\xD0\xB4";
    if (kind == "cheer")
        return "\xF0\x9F\x92\x8E \xD0\x94\xD0\xBE\xD0\xBD\xD0\xB0\xD1\x82 \xD0\xB1\xD0\xB8\xD1\x82\xD1\x81\xD0\xB0\xD0\xBC\xD0\xB8";
    return kind;
}

inline void notify_chat(const std::string& bot_token, const std::string& chat_id, const std::string& platform,
                         const std::string& author, const std::string& text) {
    send_message(bot_token, chat_id, platform_label(platform) + " | " + author + ":\n" + text);
}

inline void notify_event(const std::string& bot_token, const std::string& chat_id, const std::string& kind,
                          const std::string& user, const std::string& detail) {
    std::string msg = event_label(kind) + "\n" + user;
    if (!detail.empty()) msg += " \xE2\x80\x94 " + detail;
    send_message(bot_token, chat_id, msg);
}

inline const std::string kHelpText =
    "\xD0\x9A\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x8B:\n"
    "/mute <\xD0\xBD\xD0\xB8\xD0\xBA> \xE2\x80\x94 \xD0\xBD\xD0\xB5 \xD1\x87\xD0\xB8\xD1\x82\xD0\xB0\xD1\x82\xD1\x8C \xD0\xB8 \xD0\xBD\xD0\xB5 \xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD1\x81\xD1\x8B\xD0\xBB\xD0\xB0\xD1\x82\xD1\x8C \xD1\x81\xD0\xBE\xD0\xBE\xD0\xB1\xD1\x89\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F \xD0\xBD\xD0\xB8\xD0\xBA\xD0\xB0\n"
    "/unmute <\xD0\xBD\xD0\xB8\xD0\xBA> \xE2\x80\x94 \xD1\x81\xD0\xBD\xD1\x8F\xD1\x82\xD1\x8C \xD0\xBC\xD1\x8C\xD1\x8E\xD1\x82\n"
    "/muted \xE2\x80\x94 \xD1\x81\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD0\xB7\xD0\xB0\xD0\xBC\xD1\x8C\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8B\xD1\x85\n"
    "/skip \xE2\x80\x94 \xD0\xBF\xD1\x80\xD0\xB5\xD1\x80\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C \xD1\x82\xD0\xB5\xD0\xBA\xD1\x83\xD1\x89\xD1\x83\xD1\x8E \xD0\xBE\xD0\xB7\xD0\xB2\xD1\x83\xD1\x87\xD0\xBA\xD1\x83 \xD0\xB8 \xD0\xBE\xD1\x87\xD0\xB8\xD1\x81\xD1\x82\xD0\xB8\xD1\x82\xD1\x8C \xD0\xBE\xD1\x87\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB4\xD1\x8C\n"
    "/volume <0-200> \xE2\x80\x94 \xD0\xB3\xD1\x80\xD0\xBE\xD0\xBC\xD0\xBA\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C TTS (100 = \xD0\xBE\xD0\xB1\xD1\x8B\xD1\x87\xD0\xBD\xD0\xB0\xD1\x8F)";

inline std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

inline void handle_command(const std::string& bot_token, const std::string& chat_id, const std::string& text,
                            ModerationState& moderation, tts::TtsWorker* tts) {
    size_t sp = text.find_first_of(" \t");
    std::string command = trim(sp == std::string::npos ? text : text.substr(0, sp));
    std::string arg = trim(sp == std::string::npos ? "" : text.substr(sp + 1));

    size_t at = command.find('@');
    if (at != std::string::npos) command = command.substr(0, at);
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);

    if (command == "/mute") {
        if (arg.empty()) {
            send_message(bot_token, chat_id, "\xD0\x98\xD1\x81\xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5: /mute <\xD0\xBD\xD0\xB8\xD0\xBA>");
            return;
        }
        moderation.mute(arg);
        send_message(bot_token, chat_id, "\xF0\x9F\x94\x87 " + arg + " \xD0\xB7\xD0\xB0\xD0\xBC\xD1\x8C\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD");
    } else if (command == "/unmute") {
        if (arg.empty()) {
            send_message(bot_token, chat_id, "\xD0\x98\xD1\x81\xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5: /unmute <\xD0\xBD\xD0\xB8\xD0\xBA>");
            return;
        }
        moderation.unmute(arg);
        send_message(bot_token, chat_id, "\xF0\x9F\x94\x8A " + arg + " \xD1\x80\xD0\xB0\xD0\xB7\xD0\xBC\xD1\x8C\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD");
    } else if (command == "/muted") {
        auto names = moderation.list_muted();
        std::string joined;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i) joined += ", ";
            joined += names[i];
        }
        send_message(bot_token, chat_id,
                     "\xD0\x97\xD0\xB0\xD0\xBC\xD1\x8C\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\xD1\x8B: " + (joined.empty() ? "\xD0\xBD\xD0\xB8\xD0\xBA\xD1\x82\xD0\xBE" : joined));
    } else if (command == "/skip") {
        bool stopped = tts && tts->skip_current();
        int cleared = tts ? tts->clear_queue() : 0;
        send_message(bot_token, chat_id,
                     "\xE2\x8F\xAD \xD0\x9F\xD1\x80\xD0\xB5\xD1\x80\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xBE \xD1\x82\xD0\xB5\xD0\xBA\xD1\x83\xD1\x89\xD0\xB5\xD0\xB5: " +
                         std::string(stopped ? "\xD0\xB4\xD0\xB0" : "\xD0\xBD\xD0\xB5\xD1\x82") +
                         ". \xD0\xA3\xD0\xB1\xD1\x80\xD0\xB0\xD0\xBD\xD0\xBE \xD0\xB8\xD0\xB7 \xD0\xBE\xD1\x87\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB4\xD0\xB8: " +
                         std::to_string(cleared));
    } else if (command == "/volume") {
        bool all_digits = !arg.empty() && std::all_of(arg.begin(), arg.end(), [](unsigned char c) { return std::isdigit(c); });
        if (!all_digits) {
            send_message(bot_token, chat_id, "\xD0\x98\xD1\x81\xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5: /volume <0-200>");
            return;
        }
        int percent = std::stoi(arg);
        if (tts) tts->set_volume_percent(percent);
        send_message(bot_token, chat_id, "\xF0\x9F\x94\x8A \xD0\x93\xD1\x80\xD0\xBE\xD0\xBC\xD0\xBA\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C: " + std::to_string(percent) + "%");
    } else if (command == "/help" || command == "/start") {
        send_message(bot_token, chat_id, kHelpText);
    }
}

inline void watch_telegram_commands(const std::string& bot_token, const std::string& admin_chat_id,
                                     ModerationState& moderation, tts::TtsWorker* tts) {
    auto cli = make_client();
    cli.set_read_timeout(35);
    long long offset = 0;

    while (true) {
        try {
            std::string path = "/bot" + bot_token + "/getUpdates?offset=" + std::to_string(offset) + "&timeout=25";
            auto resp = cli.Get(path);
            if (!resp || resp->status != 200) {
                CROW_LOG_ERROR << "\xD0\x9E\xD1\x88\xD0\xB8\xD0\xB1\xD0\xBA\xD0\xB0 \xD0\xBE\xD0\xBF\xD1\x80\xD0\xBE\xD1\x81\xD0\xB0 \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4 Telegram, \xD0\xBF\xD0\xBE\xD0\xB2\xD1\x82\xD0\xBE\xD1\x80 \xD1\x87\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB7 5 \xD1\x81\xD0\xB5\xD0\xBA\xD1\x83\xD0\xBD\xD0\xB4";
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            auto data = crow::json::load(resp->body);
            if (!data || !data.has("result")) continue;

            for (const auto& update : data["result"]) {
                if (update.has("update_id")) offset = static_cast<long long>(update["update_id"].i()) + 1;

                const crow::json::rvalue* message = nullptr;
                crow::json::rvalue msg_holder;
                if (update.has("message")) {
                    msg_holder = update["message"];
                    message = &msg_holder;
                } else if (update.has("channel_post")) {
                    msg_holder = update["channel_post"];
                    message = &msg_holder;
                } else {
                    continue;
                }

                if (!message->has("chat") || !(*message)["chat"].has("id")) continue;
                std::string chat_id = std::to_string(static_cast<long long>((*message)["chat"]["id"].i()));
                if (chat_id != admin_chat_id) continue;

                if (!message->has("text")) continue;
                std::string text = trim(std::string((*message)["text"].s()));
                if (text.empty() || text[0] != '/') continue;

                try {
                    handle_command(bot_token, chat_id, text, moderation, tts);
                } catch (const std::exception& e) {
                    CROW_LOG_ERROR << "\xD0\x9E\xD1\x88\xD0\xB8\xD0\xB1\xD0\xBA\xD0\xB0 \xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB1\xD0\xBE\xD1\x82\xD0\xBA\xD0\xB8 \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x8B Telegram: " << e.what();
                }
            }
        } catch (const std::exception& e) {
            CROW_LOG_ERROR << "\xD0\x9E\xD1\x88\xD0\xB8\xD0\xB1\xD0\xBA\xD0\xB0 \xD0\xBE\xD0\xBF\xD1\x80\xD0\xBE\xD1\x81\xD0\xB0 \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4 Telegram: " << e.what();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

}
