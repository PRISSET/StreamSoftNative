#pragma once

// Twitch EventSub over WebSocket, mirroring softforstream/twitch_eventsub.py.
// Uses ixwebsocket for the client (Crow only provides a WS *server*).

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>
#include <ixwebsocket/IXWebSocket.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "twitch_auth.hpp"

namespace streamsoft::twitch {

using EventCallback = std::function<void(const std::string& kind, const std::string& user, const std::string& detail)>;

inline const std::string kEventSubWsUrl = "wss://eventsub.wss.twitch.tv/ws";

struct SubscriptionSpec {
    std::string type;
    std::string version;
    std::function<crow::json::wvalue(const std::string& broadcaster_id)> condition;
};

inline std::vector<SubscriptionSpec> subscription_specs() {
    return {
        {"channel.follow", "2",
         [](const std::string& bid) {
             crow::json::wvalue c;
             c["broadcaster_user_id"] = bid;
             c["moderator_user_id"] = bid;
             return c;
         }},
        {"channel.subscribe", "1",
         [](const std::string& bid) {
             crow::json::wvalue c;
             c["broadcaster_user_id"] = bid;
             return c;
         }},
        {"channel.subscription.gift", "1",
         [](const std::string& bid) {
             crow::json::wvalue c;
             c["broadcaster_user_id"] = bid;
             return c;
         }},
        {"channel.raid", "1",
         [](const std::string& bid) {
             crow::json::wvalue c;
             c["to_broadcaster_user_id"] = bid;
             return c;
         }},
        {"channel.cheer", "1",
         [](const std::string& bid) {
             crow::json::wvalue c;
             c["broadcaster_user_id"] = bid;
             return c;
         }},
    };
}

inline void subscribe(const std::string& client_id, const std::string& token, const SubscriptionSpec& spec,
                       const std::string& broadcaster_id, const std::string& session_id) {
    crow::json::wvalue body;
    body["type"] = spec.type;
    body["version"] = spec.version;
    body["condition"] = spec.condition(broadcaster_id);
    crow::json::wvalue transport;
    transport["method"] = "websocket";
    transport["session_id"] = session_id;
    body["transport"] = std::move(transport);

    auto api = make_https_client(kApiHost);
    httplib::Headers headers{{"Client-Id", client_id}, {"Authorization", "Bearer " + token}};
    auto resp = api.Post("/helix/eventsub/subscriptions", headers, body.dump(), "application/json");

    if (!resp || (resp->status != 200 && resp->status != 202)) {
        CROW_LOG_WARNING << "Не удалось подписаться на событие " << spec.type << " ("
                          << (resp ? std::to_string(resp->status) : "no response")
                          << ") — возможно не хватает прав/скоупа: " << (resp ? resp->body : "");
        return;
    }
    CROW_LOG_INFO << "Подписка на событие Twitch " << spec.type << " оформлена";
}

inline void extract_event(const std::string& sub_type, const crow::json::rvalue& event, const EventCallback& on_event) {
    auto str_or = [&](const char* key, const std::string& def) -> std::string {
        if (!event.has(key)) return def;
        std::string value = event[key].s();
        return value.empty() ? def : value;
    };

    if (sub_type == "channel.follow") {
        on_event("follow", str_or("user_name", "???"), "");
    } else if (sub_type == "channel.subscribe") {
        std::string tier = event.has("tier") ? std::string(event["tier"].s()) : "1000";
        on_event("subscribe", str_or("user_name", "???"), "уровень " + tier.substr(0, 1));
    } else if (sub_type == "channel.subscription.gift") {
        int total = event.has("total") ? static_cast<int>(event["total"].i()) : 1;
        on_event("gift_sub", str_or("user_name", "Аноним"), std::to_string(total) + " шт.");
    } else if (sub_type == "channel.raid") {
        int viewers = event.has("viewers") ? static_cast<int>(event["viewers"].i()) : 0;
        on_event("raid", str_or("from_broadcaster_user_name", "???"), std::to_string(viewers) + " зрителей");
    } else if (sub_type == "channel.cheer") {
        int bits = event.has("bits") ? static_cast<int>(event["bits"].i()) : 0;
        on_event("cheer", str_or("user_name", "Аноним"), std::to_string(bits) + " bits");
    } else {
        on_event(sub_type, str_or("user_name", "???"), "");
    }
}

inline void run_once(const std::string& channel, const std::string& client_id, const EventCallback& on_event) {
    std::string access_token = get_access_token(client_id);
    std::string broadcaster_id = get_user_id(client_id, access_token, channel);

    std::string ws_url = kEventSubWsUrl;
    bool already_subscribed = false;

    while (true) {
        std::mutex mtx;
        std::condition_variable cv;
        bool finished = false;
        std::string next_url;
        std::string error_text;

        ix::WebSocket ws;
        ws.disableAutomaticReconnection();
        ws.setUrl(ws_url);

        ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg) {
            // Runs on ixwebsocket's own worker thread — an uncaught exception
            // here would std::terminate() the whole process, not just this
            // worker, so every path back out must be exception-safe.
            try {
                if (msg->type == ix::WebSocketMessageType::Error) {
                    std::lock_guard<std::mutex> lock(mtx);
                    error_text = msg->errorInfo.reason;
                    finished = true;
                    cv.notify_all();
                    return;
                }
                if (msg->type == ix::WebSocketMessageType::Close) {
                    std::lock_guard<std::mutex> lock(mtx);
                    finished = true;
                    cv.notify_all();
                    return;
                }
                if (msg->type != ix::WebSocketMessageType::Message) return;

                auto payload = crow::json::load(msg->str);
                if (!payload || !payload.has("metadata")) return;
                std::string msg_type =
                    payload["metadata"].has("message_type") ? std::string(payload["metadata"]["message_type"].s()) : "";

                if (msg_type == "session_welcome") {
                    std::string session_id = std::string(payload["payload"]["session"]["id"].s());
                    CROW_LOG_INFO << "Twitch EventSub подключён (session_id=" << session_id << ")";
                    if (!already_subscribed) {
                        for (const auto& spec : subscription_specs()) {
                            subscribe(client_id, access_token, spec, broadcaster_id, session_id);
                        }
                        already_subscribed = true;
                    }
                } else if (msg_type == "notification") {
                    std::string sub_type = std::string(payload["payload"]["subscription"]["type"].s());
                    extract_event(sub_type, payload["payload"]["event"], on_event);
                } else if (msg_type == "session_reconnect") {
                    CROW_LOG_INFO << "Twitch запросил переподключение EventSub";
                    std::lock_guard<std::mutex> lock(mtx);
                    next_url = std::string(payload["payload"]["session"]["reconnect_url"].s());
                    finished = true;
                    cv.notify_all();
                } else if (msg_type == "revocation") {
                    CROW_LOG_WARNING << "Twitch отозвал подписку EventSub";
                }
                // session_keepalive: ничего не делаем.
            } catch (const std::exception& e) {
                CROW_LOG_ERROR << "Не удалось обработать сообщение EventSub: " << e.what();
            }
        });

        ws.start();
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&] { return finished; });
        }
        ws.stop();

        if (!next_url.empty()) {
            ws_url = next_url;
            continue;
        }

        throw std::runtime_error(error_text.empty() ? "Twitch EventSub WebSocket закрыт сервером"
                                                      : ("Twitch EventSub WebSocket ошибка: " + error_text));
    }
}

// Blocking; call from its own thread. Retries forever with a 20s backoff,
// same as the Python reference.
inline void watch_twitch_events(const std::string& channel, const std::string& client_id, const EventCallback& on_event) {
    while (true) {
        try {
            run_once(channel, client_id, on_event);
        } catch (const std::exception& e) {
            CROW_LOG_ERROR << "Ошибка Twitch EventSub, повтор через 20 секунд: " << e.what();
            std::this_thread::sleep_for(std::chrono::seconds(20));
        }
    }
}

} // namespace streamsoft::twitch
