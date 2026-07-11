#pragma once

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

inline void install_file_logger() {
    static FileLogHandler handler("streamsoft.log");
    crow::logger::setHandler(&handler);
}

}
