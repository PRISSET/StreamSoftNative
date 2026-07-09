#pragma once

// Twitch IRC chat reader over TLS, mirroring softforstream/twitch_chat.py —
// including the outgoing PRIVMSG write path for chat-command replies.

#include "app_paths.hpp"

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <crow/logging.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <istream>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "outgoing_queue.hpp"
#include "twitch_auth.hpp"

namespace streamsoft::twitch {

using ChatCallback = std::function<void(const std::string& author, const std::string& text)>;

inline void connect_and_listen(const std::string& channel, const std::string& nick,
                                const std::string& access_token, const ChatCallback& on_message,
                                OutgoingQueue* outgoing) {
    asio::io_context io;
    asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
    ssl_ctx.load_verify_file(resolve_resource_file("certs/cacert.pem", STREAMSOFT_CACERT_PATH));

    asio::ssl::stream<asio::ip::tcp::socket> stream(io, ssl_ctx);
    stream.set_verify_mode(asio::ssl::verify_peer);
    stream.set_verify_callback(asio::ssl::host_name_verification("irc.chat.twitch.tv"));
    SSL_set_tlsext_host_name(stream.native_handle(), "irc.chat.twitch.tv");

    asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve("irc.chat.twitch.tv", "6697");
    asio::connect(stream.next_layer(), endpoints);
    stream.handshake(asio::ssl::stream_base::client);

    // The read loop (this function, main thread) and the writer thread below
    // both touch `stream` — serialize actual socket writes through this
    // mutex so PONG replies and PRIVMSG command replies never interleave
    // their bytes on the wire (OpenSSL requires one writer at a time).
    std::mutex write_mutex;
    auto send_line = [&](const std::string& line) {
        std::string data = line + "\r\n";
        std::lock_guard<std::mutex> lock(write_mutex);
        asio::write(stream, asio::buffer(data));
    };

    send_line("PASS oauth:" + access_token);
    send_line("NICK " + nick);
    send_line("JOIN #" + channel);
    CROW_LOG_INFO << "Отправлен хендшейк Twitch IRC как " << nick << ", ждём подтверждения от сервера...";

    std::atomic<bool> connection_alive{true};
    std::thread writer_thread;
    if (outgoing) {
        writer_thread = std::thread([&] {
            std::string text;
            while (connection_alive) {
                if (!outgoing->pop_for(std::chrono::milliseconds(200), text)) continue;
                if (!connection_alive) break;
                try {
                    std::istringstream lines(text);
                    std::string line;
                    bool sent_any = false;
                    while (std::getline(lines, line)) {
                        if (line.empty()) continue;
                        send_line("PRIVMSG #" + channel + " :" + line);
                        sent_any = true;
                    }
                    if (!sent_any) send_line("PRIVMSG #" + channel + " :" + text);
                } catch (const std::exception& e) {
                    CROW_LOG_ERROR << "Не удалось отправить сообщение в Twitch чат: " << e.what();
                }
            }
        });
    }

    struct StopWriter {
        std::atomic<bool>& alive;
        std::thread& t;
        ~StopWriter() {
            alive = false;
            if (t.joinable()) t.join();
        }
    } stop_writer{connection_alive, writer_thread};

    static const std::regex privmsg_re(R"(^:([^!]+)!\S+\s+PRIVMSG\s+#\S+\s+:(.*)$)");
    const std::string join_prefix = ":" + nick + "!";

    asio::streambuf buf;
    while (true) {
        asio::read_until(stream, buf, "\r\n");
        std::istream is(&buf);
        std::string line;
        std::getline(is, line);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line.rfind("PING", 0) == 0) {
            send_line("PONG" + line.substr(4));
            continue;
        }

        if (line.find("Login authentication failed") != std::string::npos ||
            line.find("Improperly formatted auth") != std::string::npos) {
            throw std::runtime_error("Twitch отклонил авторизацию: " + line);
        }

        if (line.find(" JOIN #") != std::string::npos && line.rfind(join_prefix, 0) == 0) {
            CROW_LOG_INFO << "Twitch чат подключён (#" << channel << ")";
            continue;
        }

        std::smatch m;
        if (std::regex_match(line, m, privmsg_re)) {
            on_message(m[1].str(), m[2].str());
        }
    }
}

// Blocking; call from its own thread. Retries forever with a 15s backoff,
// same as the Python reference — meant to be resilient to transient
// network/auth hiccups without taking the whole process down. `outgoing`
// may be null if no chat-command replies need to be sent.
inline void watch_twitch(const std::string& channel, const std::string& client_id, const ChatCallback& on_message,
                          OutgoingQueue* outgoing = nullptr) {
    while (true) {
        try {
            std::string access_token = get_access_token(client_id);
            std::string nick = get_username(client_id, access_token);
            connect_and_listen(channel, nick, access_token, on_message, outgoing);
        } catch (const std::exception& e) {
            CROW_LOG_ERROR << "Ошибка Twitch чата, повтор через 15 секунд: " << e.what();
            std::this_thread::sleep_for(std::chrono::seconds(15));
        }
    }
}

} // namespace streamsoft::twitch
