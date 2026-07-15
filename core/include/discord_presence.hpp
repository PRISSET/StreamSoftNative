#pragma once

#include <crow/json.h>
#include <crow/logging.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace streamsoft::discord {

inline constexpr const char* kClientId = "1524582789020651651";
inline constexpr const char* kRepoUrl = "https://github.com/PRISSET/StreamSoftNative";

enum class Opcode : std::uint32_t {
    Handshake = 0,
    Frame = 1,
};

struct ActivityState {
    std::mutex mutex;
    std::string details = "StreamSoft";
    std::string state = "Настройка подключений";
};

inline ActivityState& activity_state() {
    static ActivityState s;
    return s;
}

inline void set_activity(const std::string& details, const std::string& state) {
    auto& s = activity_state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.details = details;
    s.state = state;
}

namespace detail {

inline bool write_frame(HANDLE pipe, Opcode op, const std::string& json) {
    std::uint32_t opcode = static_cast<std::uint32_t>(op);
    std::uint32_t length = static_cast<std::uint32_t>(json.size());
    DWORD written = 0;
    if (!WriteFile(pipe, &opcode, sizeof(opcode), &written, nullptr) || written != sizeof(opcode)) return false;
    if (!WriteFile(pipe, &length, sizeof(length), &written, nullptr) || written != sizeof(length)) return false;
    if (!json.empty()) {
        if (!WriteFile(pipe, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) ||
            written != json.size())
            return false;
    }
    return true;
}

inline bool read_frame(HANDLE pipe, Opcode& op, std::string& payload) {
    std::uint32_t opcode = 0, length = 0;
    DWORD read = 0;
    if (!ReadFile(pipe, &opcode, sizeof(opcode), &read, nullptr) || read != sizeof(opcode)) return false;
    if (!ReadFile(pipe, &length, sizeof(length), &read, nullptr) || read != sizeof(length)) return false;
    payload.assign(length, '\0');
    if (length > 0) {
        if (!ReadFile(pipe, payload.data(), length, &read, nullptr) || read != length) return false;
    }
    op = static_cast<Opcode>(opcode);
    return true;
}

inline HANDLE connect_pipe() {
    for (int i = 0; i < 10; ++i) {
        std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
    }
    return INVALID_HANDLE_VALUE;
}

inline std::string make_nonce() {
    static std::atomic<std::uint64_t> counter{0};
    auto n = counter.fetch_add(1);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    return std::to_string(now_ms) + "-" + std::to_string(n);
}

}

inline void run_discord_presence(std::string client_id, std::string repo_url) {
    using namespace std::chrono_literals;

    while (true) {
        HANDLE pipe = detail::connect_pipe();
        if (pipe == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(15s);
            continue;
        }

        crow::json::wvalue handshake;
        handshake["v"] = 1;
        handshake["client_id"] = client_id;
        if (!detail::write_frame(pipe, Opcode::Handshake, handshake.dump())) {
            CloseHandle(pipe);
            std::this_thread::sleep_for(15s);
            continue;
        }

        Opcode op;
        std::string payload;
        if (!detail::read_frame(pipe, op, payload)) {
            CloseHandle(pipe);
            std::this_thread::sleep_for(15s);
            continue;
        }
        CROW_LOG_INFO << "Discord Rich Presence подключён";

        bool alive = true;
        while (alive) {
            std::string details, state;
            {
                auto& s = activity_state();
                std::lock_guard<std::mutex> lock(s.mutex);
                details = s.details;
                state = s.state;
            }

            crow::json::wvalue activity;
            activity["state"] = state;
            activity["details"] = details;
            if (!repo_url.empty()) {
                crow::json::wvalue button;
                button["label"] = "Скачать на GitHub";
                button["url"] = repo_url;
                std::vector<crow::json::wvalue> buttons;
                buttons.push_back(std::move(button));
                activity["buttons"] = std::move(buttons);
            }

            crow::json::wvalue args;
            args["pid"] = static_cast<int>(GetCurrentProcessId());
            args["activity"] = std::move(activity);

            crow::json::wvalue frame;
            frame["cmd"] = "SET_ACTIVITY";
            frame["args"] = std::move(args);
            frame["nonce"] = detail::make_nonce();

            if (!detail::write_frame(pipe, Opcode::Frame, frame.dump())) {
                alive = false;
                break;
            }

            Opcode reply_op;
            std::string reply_payload;
            if (!detail::read_frame(pipe, reply_op, reply_payload)) {
                alive = false;
                break;
            }

            std::this_thread::sleep_for(15s);
        }

        CloseHandle(pipe);
        CROW_LOG_WARNING << "Discord Rich Presence: соединение потеряно, переподключение через 15 сек";
        std::this_thread::sleep_for(15s);
    }
}

}
