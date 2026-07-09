#pragma once

// The whole background-service bundle (chat workers, TTS, overlay HTTP/WS
// server) as a single reusable entry point — run_core() is called from
// core/src/main.cpp (standalone headless exe) *and* from gui/main.cpp (the
// merged single-exe build) on its own thread, so both binaries share one
// implementation instead of drifting apart.

#include "app_paths.hpp"
#include "auto_update.hpp"
#include "connections_config.hpp"
#include "discord_presence.hpp"
#include "outgoing_queue.hpp"
#include "overlay_server.hpp"
#include "runtime_settings.hpp"
#include "telegram.hpp"
#include "tts_launcher.hpp"
#include "tts_worker.hpp"
#include "twitch_chat.hpp"
#include "twitch_eventsub.hpp"
#include "youtube_chat.hpp"

#include <crow/logging.h>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

namespace streamsoft {

namespace detail {

// Belt-and-suspenders around workers that already retry internally (see
// twitch_chat.hpp / youtube_chat.hpp) — mirrors the _supervise() pattern in
// softforstream/main.py so nothing escaping a worker's own loop can silently
// kill that thread for good.
inline void supervise(const std::string& name, const std::function<void()>& fn) {
    while (true) {
        try {
            fn();
            return;
        } catch (const std::exception& e) {
            CROW_LOG_ERROR << "Задача '" << name << "' упала: " << e.what() << ", перезапуск через 5 сек";
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

inline std::string event_speech(const std::string& kind, const std::string& user, const std::string& detail) {
    if (kind == "follow") return user + " подписался на канал";
    if (kind == "subscribe") return user + " оформил подписку, " + detail;
    if (kind == "gift_sub") return user + " подарил подписки, " + detail;
    if (kind == "raid") return "Рейд от " + user + ", " + detail;
    if (kind == "cheer") return user + " задонатил битсами, " + detail;
    return user + ": событие " + kind;
}

} // namespace detail

// Blocking — call on its own thread if the caller also needs to run its own
// event loop (e.g. Qt's) on the calling thread.
inline void run_core() {
    using namespace detail;

    ensure_writable_config_cwd();

    auto config = ConnectionsConfig::load();

    OverlayServer overlay(8099, resolve_resource_dir("web", STREAMSOFT_WEB_DIR));

    constexpr int kTtsPort = 8102;
    auto runtime = RuntimeSettings::load();
    tts::TtsWorker tts(kTtsPort, runtime.tts_voice_ru, runtime.tts_voice_en, runtime.tts_rate,
                        runtime.tts_say_author, config.tts_max_chars);
    tts.set_volume_percent(runtime.tts_volume);
    tts.set_enabled(config.tts_enabled);
    auto tts_process = tts::start(kTtsPort);
    tts.start();
    overlay.set_tts_worker(&tts);

    std::vector<std::thread> workers;

    OutgoingQueue twitch_outgoing;

    if (config.should_run_twitch_chat()) {
        workers.emplace_back([&overlay, &twitch_outgoing, &tts, &config] {
            supervise("twitch-chat", [&overlay, &twitch_outgoing, &tts, &config] {
                twitch::watch_twitch(
                    config.twitch_channel, config.twitch_client_id,
                    [&overlay, &twitch_outgoing, &tts, &config](const std::string& author, const std::string& text) {
                        if (overlay.is_muted(author)) return;
                        overlay.broadcast_chat("twitch", author, text);
                        tts.say(author, text);
                        if (config.should_run_telegram()) {
                            telegram::notify_chat(config.telegram_bot_token, config.telegram_chat_id, "twitch",
                                                   author, text);
                        }

                        auto reply = overlay.match_command(text);
                        if (reply) twitch_outgoing.push(*reply);
                    },
                    &twitch_outgoing);
            });
        });
    } else if (!config.has_twitch()) {
        CROW_LOG_WARNING << "TWITCH_CLIENT_ID/TWITCH_CHANNEL не заданы — Twitch чат отключён";
    } else {
        CROW_LOG_INFO << "Twitch чат выключен в настройках";
    }

    if (config.should_run_twitch_eventsub()) {
        workers.emplace_back([&overlay, &tts, &config] {
            supervise("twitch-eventsub", [&overlay, &tts, &config] {
                twitch::watch_twitch_events(
                    config.twitch_channel, config.twitch_client_id,
                    [&overlay, &tts, &config](const std::string& kind, const std::string& user,
                                               const std::string& detail) {
                        overlay.broadcast_event(kind, user, detail);
                        tts.say_event(event_speech(kind, user, detail));
                        if (config.should_run_telegram()) {
                            telegram::notify_event(config.telegram_bot_token, config.telegram_chat_id, kind, user,
                                                    detail);
                        }
                    });
            });
        });
    }

    if (config.should_run_youtube()) {
        workers.emplace_back([&overlay, &tts, &config] {
            supervise("youtube-chat", [&overlay, &tts, &config] {
                youtube::watch_youtube(
                    config.youtube_video_id, config.youtube_api_key,
                    [&overlay, &tts, &config](const std::string& author, const std::string& text) {
                        if (overlay.is_muted(author)) return;
                        overlay.broadcast_chat("youtube", author, text);
                        tts.say(author, text);
                        if (config.should_run_telegram()) {
                            telegram::notify_chat(config.telegram_bot_token, config.telegram_chat_id, "youtube",
                                                   author, text);
                        }
                    });
            });
        });
    } else if (!config.has_youtube()) {
        CROW_LOG_WARNING << "YOUTUBE_API_KEY/YOUTUBE_VIDEO_ID не заданы — YouTube чат отключён";
    } else {
        CROW_LOG_INFO << "YouTube чат выключен в настройках";
    }

    if (config.should_run_telegram_control()) {
        workers.emplace_back([&overlay, &tts, &config] {
            supervise("telegram-control", [&overlay, &tts, &config] {
                telegram::watch_telegram_commands(config.telegram_bot_token, config.telegram_chat_id,
                                                   overlay.moderation(), &tts);
            });
        });
    }

    // Always on — not a per-user setting, see discord_presence.hpp's
    // kClientId/kRepoUrl.
    {
        std::string state_line;
        if (config.should_run_twitch_chat()) state_line = "Twitch: " + config.twitch_channel;
        else if (config.should_run_youtube()) state_line = "YouTube: подключено";
        else state_line = "Настройка подключений";
        discord::set_activity("StreamSoft — стрим-ассистент", state_line);

        std::thread([] {
            supervise("discord-presence", [] { discord::run_discord_presence(discord::kClientId, discord::kRepoUrl); });
        }).detach();
    }

    // Always on, same as Discord presence above — not a per-user setting.
    std::thread([] { supervise("auto-update", [] { run_auto_updater(); }); }).detach();

    for (auto& t : workers) t.detach();

    overlay.run(); // blocking
    tts::stop(tts_process);
}

} // namespace streamsoft
