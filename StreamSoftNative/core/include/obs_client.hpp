#pragma once

// obs-websocket v5 client — no OBS plugin needed, OBS 28+ ships this
// protocol built in (Tools -> WebSocket Server Settings). Handles the
// Hello/Identify auth handshake and blocking request/response, then a
// higher-level ensure_browser_sources() that creates or updates all four
// overlay Browser Sources (/chat, /events, /poll, /nowplaying) sized to the
// current canvas.

#include <crow/json.h>
#include <crow/logging.h>
#include <ixwebsocket/IXWebSocket.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace streamsoft::obs {

inline std::string base64_encode(const unsigned char* data, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_push(b64, mem);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);
    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

// obs-websocket auth: base64(sha256(base64(sha256(password + salt)) + challenge))
inline std::string sha256_base64(const std::string& input) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    return base64_encode(hash, hash_len);
}

class ObsClient {
public:
    // Native sizes for all four widget sources — independent of canvas
    // resolution, see the comment on ensure_browser_sources() below. Each
    // page CSS-positions its own content with `position: fixed` at a fixed
    // pixel offset (see style.css), which works at any viewport size, so
    // these just need to be "big enough" for that content's worst case:
    // Chat fits MAX_BUBBLES (8, see app.js) stacked bubbles, Alerts fits a
    // couple of concurrently-visible event cards, Poll fits its max of 9
    // options (see poll.hpp's start()), Now Playing matches the player's
    // natural 16:9 aspect.
    static constexpr int kChatWidth = 940;
    static constexpr int kChatHeight = 960;
    static constexpr int kAlertsWidth = 820;
    static constexpr int kAlertsHeight = 900;
    static constexpr int kPollWidth = 640;
    static constexpr int kPollHeight = 680;
    static constexpr int kNowPlayingWidth = 480;
    static constexpr int kNowPlayingHeight = 270;

    ~ObsClient() { ws_.stop(); }

    // Blocking. Throws std::runtime_error on any failure to connect/auth.
    void connect(const std::string& host = "127.0.0.1", int port = 4455, const std::string& password = "") {
        ws_.setUrl("ws://" + host + ":" + std::to_string(port));
        ws_.disableAutomaticReconnection();

        std::unique_lock<std::mutex> lock(mutex_);
        identified_ = false;
        failed_ = false;
        fail_reason_.clear();

        ws_.setOnMessageCallback([this, password](const ix::WebSocketMessagePtr& msg) { on_message(msg, password); });
        ws_.start();

        if (!cv_.wait_for(lock, std::chrono::seconds(5), [&] { return identified_ || failed_; })) {
            throw std::runtime_error("Таймаут подключения к OBS (obs-websocket на " + host + ":" + std::to_string(port) + ")");
        }
        if (failed_) {
            throw std::runtime_error(fail_reason_.empty() ? "Не удалось подключиться к OBS" : fail_reason_);
        }
    }

    void disconnect() { ws_.stop(); }

    crow::json::rvalue request(const std::string& request_type, crow::json::wvalue request_data = crow::json::wvalue()) {
        std::string request_id = std::to_string(++request_counter_);

        crow::json::wvalue d;
        d["requestType"] = request_type;
        d["requestId"] = request_id;
        d["requestData"] = std::move(request_data);
        crow::json::wvalue frame;
        frame["op"] = 6;
        frame["d"] = std::move(d);

        std::unique_lock<std::mutex> lock(mutex_);
        pending_.erase(request_id);
        lock.unlock();
        ws_.send(frame.dump());
        lock.lock();

        bool got = cv_.wait_for(lock, std::chrono::seconds(5), [&] { return pending_.count(request_id) > 0; });
        if (!got) {
            throw std::runtime_error("Таймаут ответа OBS на запрос " + request_type);
        }
        std::string raw = pending_[request_id];
        pending_.erase(request_id);
        lock.unlock();

        auto parsed = crow::json::load(raw);
        auto d_field = parsed["d"];
        bool ok = d_field.has("requestStatus") && d_field["requestStatus"].has("result") && d_field["requestStatus"]["result"].b();
        if (!ok) {
            std::string comment = (d_field.has("requestStatus") && d_field["requestStatus"].has("comment"))
                                       ? std::string(d_field["requestStatus"]["comment"].s())
                                       : "unknown error";
            throw std::runtime_error("OBS отклонил запрос " + request_type + ": " + comment);
        }
        return d_field["responseData"];
    }

    // Creates (or updates, if already present) four Browser Sources in the
    // current scene — /chat, /events, /poll, /nowplaying — each sized to its
    // own native widget size (see the k*Width/k*Height constants above),
    // not the stream canvas. Sizing a small persistent widget to the full
    // canvas left the actual content a tiny, often-blurry postage stamp
    // inside a huge transform box, with no natural way to resize/reposition
    // it in OBS without fighting that mismatch — a fixed native size means
    // what you see in OBS's Properties/Transform is the widget itself,
    // scalable and positionable the normal way, same as any image/video
    // source.
    void ensure_browser_sources(int overlay_port) {
        auto scenes = request("GetSceneList");
        std::string scene_name = std::string(scenes["currentProgramSceneName"].s());

        crow::json::wvalue list_req;
        list_req["sceneName"] = scene_name;
        auto items = request("GetSceneItemList", std::move(list_req));

        auto has_source = [&](const std::string& name) {
            for (const auto& item : items["sceneItems"]) {
                if (item.has("sourceName") && std::string(item["sourceName"].s()) == name) return true;
            }
            return false;
        };

        std::string base = "http://127.0.0.1:" + std::to_string(overlay_port);
        ensure_one(scene_name, "StreamSoft Chat", base + "/chat", kChatWidth, kChatHeight, has_source("StreamSoft Chat"));
        ensure_one(scene_name, "StreamSoft Alerts", base + "/events", kAlertsWidth, kAlertsHeight,
                   has_source("StreamSoft Alerts"));
        ensure_one(scene_name, "StreamSoft Poll", base + "/poll", kPollWidth, kPollHeight,
                   has_source("StreamSoft Poll"));
        ensure_one(scene_name, "StreamSoft Now Playing", base + "/nowplaying", kNowPlayingWidth, kNowPlayingHeight,
                   has_source("StreamSoft Now Playing"));
    }

private:
    void ensure_one(const std::string& scene_name, const std::string& source_name, const std::string& url, int width,
                     int height, bool already_exists) {
        crow::json::wvalue settings;
        settings["url"] = url;
        settings["width"] = width;
        settings["height"] = height;
        settings["reroute_audio"] = false;
        settings["shutdown"] = false; // "Shutdown source when not visible" — must stay off or the WS drops on scene switch

        if (already_exists) {
            crow::json::wvalue req;
            req["inputName"] = source_name;
            req["inputSettings"] = std::move(settings);
            req["overlay"] = true;
            request("SetInputSettings", std::move(req));
            CROW_LOG_INFO << "OBS: обновлён источник " << source_name;
        } else {
            crow::json::wvalue req;
            req["sceneName"] = scene_name;
            req["inputName"] = source_name;
            req["inputKind"] = "browser_source";
            req["inputSettings"] = std::move(settings);
            req["sceneItemEnabled"] = true;
            request("CreateInput", std::move(req));
            CROW_LOG_INFO << "OBS: создан источник " << source_name;
        }
    }

    void on_message(const ix::WebSocketMessagePtr& msg, const std::string& password) {
        try {
            if (msg->type == ix::WebSocketMessageType::Error) {
                std::lock_guard<std::mutex> lock(mutex_);
                failed_ = true;
                fail_reason_ = "WebSocket ошибка: " + msg->errorInfo.reason;
                cv_.notify_all();
                return;
            }
            if (msg->type == ix::WebSocketMessageType::Close) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!identified_) {
                    failed_ = true;
                    fail_reason_ = "Соединение с OBS закрыто до идентификации";
                }
                cv_.notify_all();
                return;
            }
            if (msg->type != ix::WebSocketMessageType::Message) return;

            auto payload = crow::json::load(msg->str);
            if (!payload || !payload.has("op")) return;
            int op = static_cast<int>(payload["op"].i());

            if (op == 0) { // Hello
                crow::json::wvalue identify;
                identify["rpcVersion"] = 1;
                identify["eventSubscriptions"] = 0;

                if (payload["d"].has("authentication")) {
                    if (password.empty()) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        failed_ = true;
                        fail_reason_ = "У OBS включён пароль на WebSocket-сервере, а он не задан";
                        cv_.notify_all();
                        return;
                    }
                    std::string challenge = std::string(payload["d"]["authentication"]["challenge"].s());
                    std::string salt = std::string(payload["d"]["authentication"]["salt"].s());
                    std::string secret = sha256_base64(password + salt);
                    identify["authentication"] = sha256_base64(secret + challenge);
                }

                crow::json::wvalue frame;
                frame["op"] = 1;
                frame["d"] = std::move(identify);
                ws_.send(frame.dump());
            } else if (op == 2) { // Identified
                std::lock_guard<std::mutex> lock(mutex_);
                identified_ = true;
                cv_.notify_all();
            } else if (op == 7) { // RequestResponse
                std::string request_id = std::string(payload["d"]["requestId"].s());
                std::lock_guard<std::mutex> lock(mutex_);
                pending_[request_id] = msg->str;
                cv_.notify_all();
            }
        } catch (const std::exception& e) {
            CROW_LOG_ERROR << "Не удалось обработать сообщение obs-websocket: " << e.what();
        }
    }

    ix::WebSocket ws_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool identified_ = false;
    bool failed_ = false;
    std::string fail_reason_;
    std::map<std::string, std::string> pending_;
    std::atomic<int> request_counter_{0};
};

} // namespace streamsoft::obs
