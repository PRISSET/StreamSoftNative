#pragma once

#include "app_paths.hpp"

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <crow/logging.h>

#include <atomic>
#include <chrono>
#include <fstream>
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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <openssl/x509.h>
#endif

namespace streamsoft::twitch {

using ChatCallback = std::function<void(const std::string& author, const std::string& text)>;

#ifdef _WIN32
// Antivirus/corporate proxies that inspect HTTPS install their own root
// certificate into Windows' trust store, not into our bundled Mozilla CA
// bundle — cpp-httplib clients handle this automatically via Schannel (see
// make_https_client in twitch_auth.hpp), but raw asio::ssl (OpenSSL) has no
// equivalent "trust the OS store" switch, so pull the Windows ROOT store in
// by hand. Without this, IRC (and therefore chat/TTS) silently never
// connects on exactly those machines, while everything running through
// httplib still works — which is what made this easy to miss the first
// time around. Best-effort: a handful of unreadable certs in the store
// shouldn't abort the whole load.
inline void load_windows_root_certs(asio::ssl::context& ctx) {
    HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
    if (!store) return;

    X509_STORE* x509_store = SSL_CTX_get_cert_store(ctx.native_handle());
    PCCERT_CONTEXT cert_ctx = nullptr;
    while ((cert_ctx = CertEnumCertificatesInStore(store, cert_ctx)) != nullptr) {
        const unsigned char* encoded = cert_ctx->pbCertEncoded;
        X509* x509 = d2i_X509(nullptr, &encoded, cert_ctx->cbCertEncoded);
        if (x509) {
            X509_STORE_add_cert(x509_store, x509);
            X509_free(x509);
        }
    }
    CertCloseStore(store, 0);
}
#endif

inline void connect_and_listen(const std::string& channel, const std::string& nick,
                                const std::string& access_token, const ChatCallback& on_message,
                                OutgoingQueue* outgoing) {
    asio::io_context io;
    asio::ssl::context ssl_ctx(asio::ssl::context::tls_client);
    {
        // Deliberately not asio::ssl::context::load_verify_file(path) here —
        // that hands OpenSSL a narrow path string it opens via the C
        // runtime's ANSI-codepage-based fopen, which can either throw while
        // building that string (see resolve_resource_file's comment) or,
        // even once that's fixed, silently fail to find the file on a
        // machine whose ANSI code page can't represent the path at all.
        // Opening it ourselves via a native (wide, on Windows) path and
        // handing OpenSSL the raw bytes sidesteps narrow-path resolution
        // completely.
        auto cert_path = resolve_resource_path("certs/cacert.pem", STREAMSOFT_CACERT_PATH);
        std::ifstream cert_file(cert_path, std::ios::binary);
        if (!cert_file) throw std::runtime_error("Не найден cacert.pem (" + path_to_utf8(cert_path) + ")");
        std::ostringstream cert_ss;
        cert_ss << cert_file.rdbuf();
        std::string cert_pem = cert_ss.str();
        ssl_ctx.add_certificate_authority(asio::buffer(cert_pem.data(), cert_pem.size()));
    }
#ifdef _WIN32
    try {
        load_windows_root_certs(ssl_ctx);
    } catch (const std::exception& e) {
        CROW_LOG_WARNING << "Twitch IRC: не удалось прочитать системное хранилище сертификатов Windows ("
                          << e.what() << "), продолжаю только со встроенным списком";
    }
#endif

    asio::ssl::stream<asio::ip::tcp::socket> stream(io, ssl_ctx);
    stream.set_verify_mode(asio::ssl::verify_peer);
    stream.set_verify_callback(asio::ssl::host_name_verification("irc.chat.twitch.tv"));
    SSL_set_tlsext_host_name(stream.native_handle(), "irc.chat.twitch.tv");

    // Logged before each step (not just on failure) so a log capture from an
    // affected machine shows exactly which one it never got past, instead
    // of one opaque exception message from watch_twitch's top-level catch.
    CROW_LOG_INFO << "Twitch IRC: резолвим irc.chat.twitch.tv...";
    asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve("irc.chat.twitch.tv", "6697");

    CROW_LOG_INFO << "Twitch IRC: устанавливаем TCP-соединение...";
    asio::connect(stream.next_layer(), endpoints);

    CROW_LOG_INFO << "Twitch IRC: TLS-хендшейк...";
    stream.handshake(asio::ssl::stream_base::client);

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

inline void watch_twitch(const std::string& channel, const std::string& client_id, const ChatCallback& on_message,
                          OutgoingQueue* outgoing = nullptr) {
    while (true) {
        try {
            CROW_LOG_INFO << "Twitch IRC: получаем токен доступа...";
            std::string access_token = get_access_token(client_id);
            CROW_LOG_INFO << "Twitch IRC: получаем имя пользователя...";
            std::string nick = get_username(client_id, access_token);
            CROW_LOG_INFO << "Twitch IRC: имя получено (" << nick << "), подключаемся к чату...";
            connect_and_listen(channel, nick, access_token, on_message, outgoing);
        } catch (const AuthRejected& e) {
            CROW_LOG_ERROR << "Twitch отклонил токен, сбрасываю кэш и повторю авторизацию через 15 секунд: "
                            << e.what();
            invalidate_cached_token();
            std::this_thread::sleep_for(std::chrono::seconds(15));
        } catch (const std::exception& e) {
            CROW_LOG_ERROR << "Ошибка Twitch чата, повтор через 15 секунд: " << e.what();
            std::this_thread::sleep_for(std::chrono::seconds(15));
        }
    }
}

}
