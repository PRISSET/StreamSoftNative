#pragma once

// Speaks chat messages/events — mirrors softforstream/tts_worker.py: a
// single background thread consumes a queue (so lines never overlap),
// picks a voice by detected language, calls the TTS adapter over HTTP for
// synthesis, and plays the result via the same Windows MCI approach as the
// Python version (ctypes winmm there, direct winmm.lib linkage here).

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

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<QueueItem> queue_;

    std::mutex mci_mutex_;
    std::string current_alias_;
};

} // namespace streamsoft::tts
