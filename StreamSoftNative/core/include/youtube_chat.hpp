#pragma once

#include <crow/json.h>
#include <crow/logging.h>
#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

#include "twitch_auth.hpp"

namespace streamsoft::youtube {

using ChatCallback = std::function<void(const std::string& author, const std::string& text)>;

inline const std::string kApiHost = "https://www.googleapis.com";

inline std::string get_live_chat_id(httplib::Client& api, const std::string& video_id, const std::string& api_key) {
    std::string path = "/youtube/v3/videos?part=liveStreamingDetails&id=" + streamsoft::twitch::url_encode(video_id) +
                        "&key=" + streamsoft::twitch::url_encode(api_key);
    auto resp = api.Get(path);
    if (!resp) {
        throw std::runtime_error("YouTube API недоступен");
    }

    auto data = crow::json::load(resp->body);
    if (!data || !data.has("items") || data["items"].size() == 0) {
        throw std::runtime_error("Видео " + video_id + " не найдено или недоступно (проверь ID и API key)");
    }

    const auto& item = data["items"][0];
    if (!item.has("liveStreamingDetails") || !item["liveStreamingDetails"].has("activeLiveChatId")) {
        throw std::runtime_error("У видео " + video_id + " нет активного чата (стрим сейчас не идёт?)");
    }
    return std::string(item["liveStreamingDetails"]["activeLiveChatId"].s());
}

inline void poll_messages(httplib::Client& api, const std::string& live_chat_id, const std::string& api_key,
                           const ChatCallback& on_message) {
    std::string page_token;

    while (true) {
        std::string path = "/youtube/v3/liveChat/messages?liveChatId=" + streamsoft::twitch::url_encode(live_chat_id) +
                            "&part=snippet,authorDetails&key=" + streamsoft::twitch::url_encode(api_key);
        if (!page_token.empty()) {
            path += "&pageToken=" + streamsoft::twitch::url_encode(page_token);
        }

        auto resp = api.Get(path);
        if (!resp || resp->status != 200) {
            throw std::runtime_error("YouTube API вернул " + std::to_string(resp ? resp->status : -1) + ": " +
                                      (resp ? resp->body : "no response"));
        }

        auto data = crow::json::load(resp->body);
        if (data && data.has("items")) {
            for (const auto& item : data["items"]) {
                std::string text = (item.has("snippet") && item["snippet"].has("displayMessage"))
                                        ? std::string(item["snippet"]["displayMessage"].s())
                                        : "";
                std::string author = (item.has("authorDetails") && item["authorDetails"].has("displayName"))
                                          ? std::string(item["authorDetails"]["displayName"].s())
                                          : "???";
                if (!text.empty()) {
                    on_message(author, text);
                }
            }
        }

        page_token = (data && data.has("nextPageToken")) ? std::string(data["nextPageToken"].s()) : "";
        long long interval_ms = (data && data.has("pollingIntervalMillis"))
                                     ? static_cast<long long>(data["pollingIntervalMillis"].i())
                                     : 5000;
        interval_ms = std::max<long long>(interval_ms, 2000);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

inline void watch_youtube(const std::string& video_id, const std::string& api_key, const ChatCallback& on_message) {
    auto api = streamsoft::twitch::make_https_client(kApiHost);
    api.set_read_timeout(20);

    while (true) {
        try {
            std::string live_chat_id = get_live_chat_id(api, video_id, api_key);
            CROW_LOG_INFO << "YouTube чат подключён (liveChatId=" << live_chat_id << ")";
            poll_messages(api, live_chat_id, api_key, on_message);
        } catch (const std::exception& e) {
            CROW_LOG_ERROR << "Ошибка YouTube чата, повтор через 15 секунд: " << e.what();
            std::this_thread::sleep_for(std::chrono::seconds(15));
        }
    }
}

}
