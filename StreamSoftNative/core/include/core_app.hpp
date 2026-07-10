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
#include "rvc_launcher.hpp"
#include "telegram.hpp"
#include "tts_launcher.hpp"
#include "tts_worker.hpp"
#include "twitch_chat.hpp"
#include "twitch_eventsub.hpp"
#include "youtube_chat.hpp"

#include <crow/logging.h>
#include <chrono>
#include <functional>
#include <mutex>
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
    overlay.set_broadcaster_name(config.twitch_channel);

    constexpr int kTtsPort = 8102;
    auto runtime = RuntimeSettings::load();
    tts::TtsWorker tts(kTtsPort, runtime.tts_voice_ru, runtime.tts_voice_en, runtime.tts_rate,
                        runtime.tts_say_author, config.tts_max_chars);
    tts.set_volume_percent(runtime.tts_volume);
    tts.set_enabled(config.tts_enabled);
    tts.set_ducking(runtime.ducking_enabled, runtime.ducking_percent);
    auto tts_process = tts::start(kTtsPort);
    tts.start();
    overlay.set_tts_worker(&tts);

    constexpr int kRvcPort = 8103;
    auto rvc_process = rvc::start(kRvcPort);
    if (rvc_process.running) overlay.set_rvc_port(kRvcPort);

    // Seeds TtsWorker with whatever RVC settings were last saved — without
    // this, toggling "Включить смену голоса" saved to runtime_settings.json
    // just fine but speak() never actually knew about it (confirmed live:
    // page showed the adapter healthy, toggle looked enabled, TTS kept
    // playing the plain voice) since nothing ever told the worker.
    tts.set_rvc_settings(runtime.rvc_enabled, runtime.rvc_scope, runtime.rvc_pitch, runtime.rvc_index_rate,
                          runtime.rvc_protect, runtime.rvc_f0method);
    if (rvc_process.running) tts.set_rvc_port(kRvcPort);

    // Guards tts_process/rvc_process against the race between a background
    // Check&Install thread's "just finished" callback (below) writing a
    // freshly-started AdapterProcess into these and this thread reading them
    // again at shutdown (tts::stop()/rvc::stop() at the very end of this
    // function).
    std::mutex adapter_mutex;

    // Lets a module installed *after* this app already started actually
    // come alive right away — without this hook, the adapter subprocess
    // above never got spawned (is_installed() was false at that point), and
    // nothing else would ever retry it short of a full app restart.
    set_module_installed_callback("tts", [&tts_process, &adapter_mutex, kTtsPort] {
        std::lock_guard<std::mutex> lock(adapter_mutex);
        if (tts_process.running) return;
        tts_process = tts::start(kTtsPort);
        if (tts_process.running) CROW_LOG_INFO << "TTS-адаптер поднят сразу после установки, без перезапуска";
    });
    set_module_installed_callback("rvc", [&rvc_process, &overlay, &tts, &adapter_mutex, kRvcPort] {
        std::lock_guard<std::mutex> lock(adapter_mutex);
        if (rvc_process.running) return;
        rvc_process = rvc::start(kRvcPort);
        if (rvc_process.running) {
            overlay.set_rvc_port(kRvcPort);
            tts.set_rvc_port(kRvcPort);
            CROW_LOG_INFO << "RVC-адаптер поднят сразу после установки, без перезапуска";
        }
    });

    std::vector<std::thread> workers;

    OutgoingQueue twitch_outgoing;
    overlay.set_twitch_outgoing(&twitch_outgoing);

    if (config.should_run_twitch_chat()) {
        workers.emplace_back([&overlay, &twitch_outgoing, &tts, &config] {
            supervise("twitch-chat", [&overlay, &twitch_outgoing, &tts, &config] {
                twitch::watch_twitch(
                    config.twitch_channel, config.twitch_client_id,
                    [&overlay, &twitch_outgoing, &tts, &config](const std::string& author, const std::string& text) {
                        if (overlay.is_muted(author)) return;
                        if (overlay.try_poll_vote(author, text)) return;

                        auto builtin_reply = overlay.try_builtin_command(author, text);
                        if (builtin_reply) {
                            twitch_outgoing.push(*builtin_reply);
                            return;
                        }

                        auto song_reply = overlay.try_song_request(author, text);
                        if (song_reply) {
                            twitch_outgoing.push(*song_reply);
                            return;
                        }

                        overlay.broadcast_chat("twitch", author, text);
                        overlay.award_points_for_message(author);
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

        // Periodic, low-frequency nudge about the points economy and !song —
        // otherwise a viewer has no way to learn about either short of
        // reading !help themselves. Long interval and gated on the feature
        // actually being on (see song_reminder_text()) so it never reads as
        // spam the way a per-message or per-minute reminder would.
        workers.emplace_back([&overlay, &twitch_outgoing] {
            supervise("chat-reminders", [&overlay, &twitch_outgoing] {
                while (true) {
                    std::this_thread::sleep_for(std::chrono::minutes(15));
                    auto text = overlay.song_reminder_text();
                    if (text) twitch_outgoing.push(*text);
                }
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
                        if (overlay.try_poll_vote(author, text)) return;
                        // No outgoing-reply channel for YouTube (read-only
                        // worker, same as chat commands) — still recognized
                        // and consumed (points spent, command matched), the
                        // reply text just has nowhere to go.
                        if (overlay.try_builtin_command(author, text)) return;
                        if (overlay.try_song_request(author, text)) return;

                        overlay.broadcast_chat("youtube", author, text);
                        overlay.award_points_for_message(author);
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

    std::lock_guard<std::mutex> lock(adapter_mutex);
    tts::stop(tts_process);
    rvc::stop(rvc_process);
}

} // namespace streamsoft
