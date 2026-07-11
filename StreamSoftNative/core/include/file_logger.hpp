#pragma once

// Crow's default ILogHandler writes to std::cerr — fine for the standalone
// dev core.exe (has a console), completely invisible for the real shipped
// app (gui/main.cpp builds WIN32 subsystem, no console, stderr goes
// nowhere). That's meant every "why did chat/RVC/YouTube just silently stop
// working" investigation on a real install needed a throwaway instrumented
// build to see anything at all. This mirrors CerrLogHandler's exact output
// format, just aimed at a file instead, so a real install is diagnosable by
// reading streamsoft.log next to its other per-user state
// (connections.json, twitch_token.json, ...) instead of needing that.

#include <crow/logging.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>

namespace streamsoft {

class FileLogHandler : public crow::ILogHandler {
public:
    explicit FileLogHandler(const std::string& path) {
        // A log that grows forever on a background service that can run
        // for weeks isn't useful past a certain size anyway — starting
        // fresh past this threshold keeps it bounded without needing a
        // real rotation scheme. Append (not truncate) below that threshold
        // so a quick crash-restart loop doesn't lose the very entries that
        // would explain the crash.
        constexpr long kMaxBytes = 2 * 1024 * 1024;
        std::error_code ec;
        bool too_big = false;
        {
            std::ifstream check(path, std::ios::binary | std::ios::ate);
            if (check && check.tellg() > static_cast<std::streamoff>(kMaxBytes)) too_big = true;
        }
        file_.open(path, std::ios::binary | (too_big ? std::ios::trunc : std::ios::app));
    }

    void log(const std::string& message, crow::LogLevel level) override {
        if (!file_) return;
        const char* level_str = "INFO    ";
        switch (level) {
            case crow::LogLevel::Debug: level_str = "DEBUG   "; break;
            case crow::LogLevel::Info: level_str = "INFO    "; break;
            case crow::LogLevel::Warning: level_str = "WARNING "; break;
            case crow::LogLevel::Error: level_str = "ERROR   "; break;
            case crow::LogLevel::Critical: level_str = "CRITICAL"; break;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        file_ << "(" << timestamp() << ") [" << level_str << "] " << message << std::endl;
    }

private:
    static std::string timestamp() {
        char buf[32];
        time_t t = time(nullptr);
        tm local_tm;
        localtime_s(&local_tm, &t);
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local_tm);
        return buf;
    }

    std::mutex mutex_;
    std::ofstream file_;
};

// Call once at startup, after ensure_writable_config_cwd() so the log lands
// next to the rest of this app's per-user state rather than wherever the
// process happened to be launched from.
inline void install_file_logger() {
    static FileLogHandler handler("streamsoft.log");
    crow::logger::setHandler(&handler);
}

}  // namespace streamsoft
