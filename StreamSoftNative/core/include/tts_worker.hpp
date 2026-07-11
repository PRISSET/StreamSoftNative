#pragma once

// Speaks chat messages/events — mirrors softforstream/tts_worker.py: a
// single background thread consumes a queue (so lines never overlap),
// picks a voice by detected language, calls the TTS adapter over HTTP for
// synthesis, and plays the result via the same Windows MCI approach as the
// Python version (ctypes winmm there, direct winmm.lib linkage here).

#include "audio_ducking.hpp"

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <thread>

#include <windows.h>
#include <mmsystem.h>

namespace streamsoft::tts {

inline const std::regex kUrlRe(R"(https?://\S+)");

// Byte-level check instead of a Unicode-range std::regex: MSVC's narrow
// string literal encoding for non-ASCII source text depends on the active
// source/execution charset, which is one more thing to get wrong for no
// benefit here — Cyrillic in UTF-8 always leads with 0xD0/0xD1 (covers the
// whole U+0400-U+04FF block, i.e. all of а-я/А-Я/ё/Ё), so this is both
// simpler and charset-independent.
inline bool has_cyrillic(const std::string& s) {
    for (unsigned char c : s) {
        if (c == 0xD0 || c == 0xD1) return true;
    }
    return false;
}

inline bool has_latin(const std::string& s) {
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    }
    return false;
}

struct QueueItem {
    std::optional<std::string> author; // nullopt for events
    std::string text;
};

class TtsWorker {
public:
    TtsWorker(int adapter_port, std::string voice_ru, std::string voice_en, std::string rate, bool say_author,
               int max_chars)
        : port_(adapter_port),
          voice_ru_(std::move(voice_ru)),
          voice_en_(std::move(voice_en)),
          rate_(std::move(rate)),
          say_author_(say_author),
          max_chars_(max_chars) {}

    void start() {
        thread_ = std::thread([this] { run(); });
    }

    void say(const std::string& author, const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({author, text});
        cv_.notify_one();
    }

    void say_event(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({std::nullopt, text});
        cv_.notify_one();
    }

    void set_voice_ru(const std::string& v) { voice_ru_ = v; }
    void set_voice_en(const std::string& v) { voice_en_ = v; }
    void set_rate(const std::string& v) { rate_ = v; }
    void set_say_author(bool v) { say_author_ = v; }
    void set_volume_percent(int percent) { volume_percent_ = std::max(0, std::min(200, percent)); }
    // Queued items still drain (so nothing pile-ups once re-enabled), they
    // just skip synthesis/playback while disabled — same idea as the
    // Python side's mute list, but for the whole subsystem.
    void set_enabled(bool v) { enabled_ = v; }

    // Set once at startup and again whenever the RVC adapter comes up after
    // a fresh Check&Install (see core_app.hpp) — 0 means "no adapter",
    // speak() then just skips conversion instead of trying a dead port.
    void set_rvc_port(int port) { rvc_port_ = port; }

    // The RVC settings page (RvcPage.qml) posts these as a group whenever
    // any one of them changes — cheaper to just re-copy all five than to
    // add a setter per field, and keeps them consistent with each other
    // (no torn reads between e.g. pitch and f0method mid-update).
    void set_rvc_settings(bool enabled, const std::string& scope, int pitch, double index_rate, double protect,
                           const std::string& f0method) {
        std::lock_guard<std::mutex> lock(rvc_mutex_);
        rvc_enabled_ = enabled;
        rvc_scope_ = scope;
        rvc_pitch_ = pitch;
        rvc_index_rate_ = index_rate;
        rvc_protect_ = protect;
        rvc_f0method_ = f0method;
    }

    void set_ducking(bool enabled, int percent) {
        ducking_enabled_ = enabled;
        ducking_percent_ = std::max(0, std::min(100, percent));
    }

    // Interrupts whatever's currently playing (Telegram-style /skip).
    bool skip_current() {
        std::lock_guard<std::mutex> lock(mci_mutex_);
        if (current_alias_.empty()) return false;
        wchar_t buf[8];
        std::wstring walias(current_alias_.begin(), current_alias_.end());
        mciSendStringW((L"stop " + walias).c_str(), buf, 8, nullptr);
        return true;
    }

    int clear_queue() {
        std::lock_guard<std::mutex> lock(mutex_);
        int n = static_cast<int>(queue_.size());
        queue_.clear();
        return n;
    }

private:
    void run() {
        // Needed for AudioDucker's MMDevice/COM calls below — this thread
        // never touches COM otherwise (MCI playback doesn't need it), so
        // nothing initialized it yet. MTA, not STA: this thread has no
        // Windows message pump (it's a plain condition-variable loop), and
        // STA COM calls can rely on message delivery for RPC completion —
        // without a pump, a call into the audio engine can stall the
        // apartment indefinitely. Core Audio's interfaces are documented as
        // agile ("Both"), so MTA is the correct/recommended choice for
        // exactly this kind of pump-less worker thread.
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        while (true) {
            QueueItem item;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return !queue_.empty(); });
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            try {
                speak(item);
            } catch (const std::exception& e) {
                CROW_LOG_ERROR << "Ошибка озвучки: " << e.what();
            }

            // Only restore once nothing else is queued — duck()'s own
            // ducked_ guard already skips the (relatively heavy) session
            // enumeration for consecutive queued lines, but that only
            // helped if something wasn't undoing it between every single
            // line. Restoring here instead of unconditionally after every
            // speak() means a fast-moving chat ducks once per burst, not
            // once per message (fewer audible blips, and far less traffic
            // through the audio engine's session APIs).
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) ducker_.restore();
        }
    }

    std::string pick_voice(const std::string& text) {
        if (has_cyrillic(text)) return voice_ru_;
        if (has_latin(text)) return voice_en_;
        return voice_ru_;
    }

    void speak(const QueueItem& item) {
        if (!enabled_) return;

        std::string clean = std::regex_replace(item.text, kUrlRe, "ссылка");
        if (clean.empty()) return;
        if (static_cast<int>(clean.size()) > max_chars_) clean = clean.substr(0, max_chars_);

        std::string phrase = (say_author_ && item.author) ? (*item.author + ": " + clean) : clean;
        std::string voice = pick_voice(clean);

        // duck()'s COM device/session enumeration (audio_ducking.hpp) is
        // the actual source of the audible stutter reported "at moments" —
        // it used to run synchronously right before play_blocking() below,
        // so its cost (every active render device, every session on each —
        // can be tens to hundreds of ms on a machine with a game, Discord,
        // browser, OBS, etc. all open) landed directly on the line's start
        // latency, once per burst. Starting it here and joining just before
        // play_blocking() overlaps it with the TTS synth call (and RVC's
        // multi-second GPU inference, when that's in the path) instead,
        // which in every realistic case is already slower than the
        // enumeration — so the duck by the time playback is ready to start.
        std::thread duck_thread;
        if (ducking_enabled_) {
            int target_percent = ducking_percent_.load();
            duck_thread = std::thread([this, target_percent] { ducker_.duck(target_percent); });
        }

        httplib::Client cli("http://127.0.0.1:" + std::to_string(port_));
        cli.set_connection_timeout(2);
        cli.set_read_timeout(20);

        int percent = volume_percent_.load();
        char vol_buf[8];
        std::snprintf(vol_buf, sizeof(vol_buf), "%+d%%", percent - 100);

        crow::json::wvalue body;
        body["text"] = phrase;
        body["voice"] = voice;
        body["rate"] = rate_;
        body["volume"] = std::string(vol_buf);

        auto res = cli.Post("/synthesize", body.dump(), "application/json");
        if (!res || res->status != 200) {
            CROW_LOG_WARNING << "TTS-адаптер недоступен, сообщение пропущено";
            // duck_thread is already running (or about to start ducking) at
            // this point — join it before bailing, or an un-joined
            // still-joinable std::thread calls std::terminate() on
            // destruction. restore() (run()'s loop, once the queue drains)
            // is unconditional, so this doesn't leave anything stuck ducked.
            if (duck_thread.joinable()) duck_thread.join();
            return;
        }

        std::string audio = res->body;
        bool is_wav = false;

        bool rvc_enabled;
        int rvc_pitch;
        double rvc_index_rate, rvc_protect;
        std::string rvc_scope, rvc_f0method;
        {
            std::lock_guard<std::mutex> lock(rvc_mutex_);
            rvc_enabled = rvc_enabled_;
            rvc_scope = rvc_scope_;
            rvc_pitch = rvc_pitch_;
            rvc_index_rate = rvc_index_rate_;
            rvc_protect = rvc_protect_;
            rvc_f0method = rvc_f0method_;
        }
        int rvc_port = rvc_port_;

        // "alerts" scope only converts events (item.author is nullopt for
        // those, see say_event()); "all" converts chat lines too.
        bool wants_conversion = rvc_enabled && rvc_port != 0 && (rvc_scope == "all" || !item.author);

        if (wants_conversion) {
            httplib::Client rvc_cli("http://127.0.0.1:" + std::to_string(rvc_port));
            rvc_cli.set_connection_timeout(2);
            // GPU inference on a short line has taken several seconds in
            // testing — generous but not unbounded, so a genuinely stuck
            // adapter doesn't stall the whole speech queue forever.
            rvc_cli.set_read_timeout(30);

            std::string query = "/convert?pitch=" + std::to_string(rvc_pitch) +
                                 "&index_rate=" + std::to_string(rvc_index_rate) +
                                 "&protect=" + std::to_string(rvc_protect) + "&f0method=" + rvc_f0method;
            auto rvc_res = rvc_cli.Post(query, audio, "audio/mpeg");
            if (rvc_res && rvc_res->status == 200) {
                audio = rvc_res->body;
                is_wav = true;
            } else {
                // Graceful degrade per CLAUDE.md §4: adapter hiccup falls
                // back to the plain TTS voice already in `audio`, doesn't
                // drop the line or take down the queue.
                CROW_LOG_WARNING << "RVC-конвертация не удалась, играю обычный голос TTS";
            }
        }

        char tmp_dir[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp_dir);
        std::string path = std::string(tmp_dir) + "streamsoft_tts_" + std::to_string(GetTickCount64()) +
                            (is_wav ? ".wav" : ".mp3");

        {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            f << audio;
        }

        // restore() happens in run(), once the queue is actually empty —
        // see the comment there. Only duck() here; toggling the setting off
        // mid-line just means this was the last line to duck for, run()'s
        // restore() call (unconditional, idempotent) still cleans it up.
        // duck_thread was kicked off back near the top of this function (see
        // that comment) — by now it's had the entire TTS/RVC round trip to
        // finish, so this join is a no-op in every realistic case.
        if (duck_thread.joinable()) duck_thread.join();
        play_blocking(path, is_wav ? "waveaudio" : "mpegvideo");
        DeleteFileA(path.c_str());
    }

    void play_blocking(const std::string& path, const std::string& mci_type) {
        std::wstring walias = L"tts_" + std::to_wstring(GetCurrentThreadId());
        std::wstring wpath(path.begin(), path.end());
        std::wstring wtype(mci_type.begin(), mci_type.end());

        {
            std::lock_guard<std::mutex> lock(mci_mutex_);
            current_alias_ = std::string(walias.begin(), walias.end());
        }

        wchar_t buf[255];
        std::wstring open_cmd = L"open \"" + wpath + L"\" type " + wtype + L" alias " + walias;
        if (mciSendStringW(open_cmd.c_str(), buf, 254, nullptr) == 0) {
            mciSendStringW((L"play " + walias + L" wait").c_str(), buf, 254, nullptr);
            mciSendStringW((L"close " + walias).c_str(), buf, 254, nullptr);
        }

        {
            std::lock_guard<std::mutex> lock(mci_mutex_);
            current_alias_.clear();
        }
    }

    int port_;
    std::string voice_ru_;
    std::string voice_en_;
    std::string rate_;
    bool say_author_;
    int max_chars_;
    std::atomic<int> volume_percent_{100};
    std::atomic<bool> enabled_{true};

    std::atomic<int> rvc_port_{0};
    std::mutex rvc_mutex_;
    bool rvc_enabled_ = false;
    std::string rvc_scope_ = "alerts";
    int rvc_pitch_ = 12;
    double rvc_index_rate_ = 0.3;
    double rvc_protect_ = 0.5;
    std::string rvc_f0method_ = "rmvpe";

    std::atomic<bool> ducking_enabled_{false};
    std::atomic<int> ducking_percent_{30};
    AudioDucker ducker_; // only ever touched from run()'s own thread, see speak()

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<QueueItem> queue_;

    std::mutex mci_mutex_;
    std::string current_alias_;
};

} // namespace streamsoft::tts
