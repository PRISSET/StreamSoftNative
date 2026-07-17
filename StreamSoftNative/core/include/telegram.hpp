#pragma once

#include "app_paths.hpp"
#include "twitch_auth.hpp"

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
    // Shares twitch::https_client_construction_mutex() — see the comment
    // there. Same httplib::Client-construction-isn't-thread-safe-on-first-
    // use issue applies to every HTTPS client in the app, not just Faceit/
    // Dota's, so this reuses the one fix instead of duplicating it.
    std::lock_guard<std::mutex> lock(twitch::https_client_construction_mutex());
    httplib::Client cli("https://api.telegram.org");
    cli.enable_server_certificate_verification(true);
#ifndef CPPHTTPLIB_WINDOWS_AUTOMATIC_ROOT_CERTIFICATES_UPDATE
    cli.set_ca_cert_path(resolve_resource_file("certs/cacert.pem", STREAMSOFT_CACERT_PATH).c_str());
#endif
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
    if (platform == "youtube") return "🔴 YouTube";
    if (platform == "twitch") return "💜 Twitch";
    return platform;
}

inline std::string event_label(const std::string& kind) {
    if (kind == "follow") return "💚 Новый фоллоу";
    if (kind == "subscribe")
        return "⭐ Новая подписка";
    if (kind == "gift_sub") return "🎁 Подарочная подписка";
    if (kind == "raid") return "🚀 Рейд";
    if (kind == "cheer")
        return "💎 Донат битсами";
    if (kind == "youtube_sub") return "⭐ Новый участник (YouTube)";
    if (kind == "youtube_sub_milestone") return "🎉 Юбилей участия (YouTube)";
    if (kind == "youtube_gift_sub") return "🎁 Подарочное участие (YouTube)";
    if (kind == "youtube_superchat") return "💰 Super Chat (YouTube)";
    if (kind == "youtube_supersticker") return "💰 Super Sticker (YouTube)";
    return kind;
}

inline void notify_chat(const std::string& bot_token, const std::string& chat_id, const std::string& platform,
                         const std::string& author, const std::string& text) {
    send_message(bot_token, chat_id, platform_label(platform) + " | " + author + ":\n" + text);
}

inline void notify_event(const std::string& bot_token, const std::string& chat_id, const std::string& kind,
                          const std::string& user, const std::string& detail) {
    std::string msg = event_label(kind) + "\n" + user;
    if (!detail.empty()) msg += " — " + detail;
    send_message(bot_token, chat_id, msg);
}

inline void notify_stream_start(const std::string& bot_token, const std::string& channel_id,
                                 const std::string& twitch_channel) {
    std::string msg = "🔴 Стрим начался!\nhttps://twitch.tv/" + twitch_channel;
    send_message(bot_token, channel_id, msg);
}

inline const std::string kHelpText =
    "Команды:\n"
    "/mute <ник> — не читать и не пересылать сообщения ника\n"
    "/unmute <ник> — снять мьют\n"
    "/muted — список замьюченных\n"
    "/skip — прервать текущую озвучку и очистить очередь\n"
    "/volume <0-200> — громкость TTS (100 = обычная)";

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
            send_message(bot_token, chat_id, "Использование: /mute <ник>");
            return;
        }
        moderation.mute(arg);
        send_message(bot_token, chat_id, "🔇 " + arg + " замьючен");
    } else if (command == "/unmute") {
        if (arg.empty()) {
            send_message(bot_token, chat_id, "Использование: /unmute <ник>");
            return;
        }
        moderation.unmute(arg);
        send_message(bot_token, chat_id, "🔊 " + arg + " размьючен");
    } else if (command == "/muted") {
        auto names = moderation.list_muted();
        std::string joined;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i) joined += ", ";
            joined += names[i];
        }
        send_message(bot_token, chat_id,
                     "Замьючены: " + (joined.empty() ? "никто" : joined));
    } else if (command == "/skip") {
        bool stopped = tts && tts->skip_current();
        int cleared = tts ? tts->clear_queue() : 0;
        send_message(bot_token, chat_id,
                     "⏭ Прервано текущее: " +
                         std::string(stopped ? "да" : "нет") +
                         ". Убрано из очереди: " +
                         std::to_string(cleared));
    } else if (command == "/volume") {
        bool all_digits = !arg.empty() && std::all_of(arg.begin(), arg.end(), [](unsigned char c) { return std::isdigit(c); });
        if (!all_digits) {
            send_message(bot_token, chat_id, "Использование: /volume <0-200>");
            return;
        }
        int percent = std::stoi(arg);
        if (tts) tts->set_volume_percent(percent);
        send_message(bot_token, chat_id, "🔊 Громкость: " + std::to_string(percent) + "%");
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
                CROW_LOG_ERROR << "Ошибка опроса команд Telegram, повтор через 5 секунд";
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
                    CROW_LOG_ERROR << "Ошибка обработки команды Telegram: " << e.what();
                }
            }
        } catch (const std::exception& e) {
            CROW_LOG_ERROR << "Ошибка опроса команд Telegram: " << e.what();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

}
