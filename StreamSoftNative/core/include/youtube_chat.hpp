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
#include "youtube_auth.hpp"

namespace streamsoft::youtube {

using ChatCallback = std::function<void(const std::string& author, const std::string& text)>;
// Mirrors twitch_eventsub.hpp's callback shape exactly (kind, user, detail)
// so core_app.hpp can route both through the same overlay.broadcast_event()
// call — the viewer-facing overlay/events.html already reads {type:"event",
// kind, user, detail} generically and doesn't care which platform it came
// from.
using EventCallback = std::function<void(const std::string& kind, const std::string& user, const std::string& detail)>;

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

// search.list's eventType=live filter looked like the obvious way to find
// "whatever this channel is streaming right now" with just an API key (no
// OAuth) — but Google's own docs say that parameter "is intended exclusively
// for YouTube content partners", and verified live against a real non-partner
// channel it just returns zero results even while actually live. Instead:
// pull the channel's most recent uploads (order=date, no eventType filter,
// works for everyone) and probe each one with get_live_chat_id() above —
// the same "does this video have an activeLiveChatId" check already used
// for a manually-entered video id — until one hits.
inline std::string resolve_live_video_id(httplib::Client& api, const std::string& channel_id, const std::string& api_key) {
    std::string path = "/youtube/v3/search?part=id&channelId=" + streamsoft::twitch::url_encode(channel_id) +
                        "&order=date&type=video&maxResults=5&key=" + streamsoft::twitch::url_encode(api_key);
    auto resp = api.Get(path);
    if (!resp) {
        throw std::runtime_error("YouTube API недоступен (поиск последних видео канала)");
    }
    if (resp->status != 200) {
        throw std::runtime_error("YouTube API вернул " + std::to_string(resp->status) +
                                  " при поиске последних видео канала: " + resp->body);
    }

    auto data = crow::json::load(resp->body);
    if (!data || !data.has("items")) {
        throw std::runtime_error("YouTube API вернул неожиданный формат при поиске последних видео");
    }

    for (const auto& item : data["items"]) {
        if (!item.has("id") || !item["id"].has("videoId")) continue;
        std::string vid = std::string(item["id"]["videoId"].s());
        try {
            get_live_chat_id(api, vid, api_key);
            return vid;
        } catch (const std::exception&) {
            continue;  // not the live one — try the next most recent upload
        }
    }
    throw std::runtime_error("У канала сейчас нет активной трансляции");
}

// snippet.type on a liveChat message is the same field whether it's an
// ordinary chat line or a members/Super Chat/Super Sticker event — YouTube
// pushes all of it through this one endpoint, we just have to branch on the
// type instead of assuming everything is textMessageEvent like before.
// Field names verified against Google's own docs (developers.google.com/
// youtube/v3/live/docs/liveChatMessages) — not yet cross-checked against a
// live payload the way the rest of this file's quirks were, since triggering
// a real membership/Super Chat costs real money to test with.
inline void route_special_event(const crow::json::rvalue& item, const std::string& author,
                                 const EventCallback& on_event) {
    if (!on_event || !item.has("snippet")) return;
    auto snippet = item["snippet"];
    std::string type = snippet.has("type") ? std::string(snippet["type"].s()) : "";

    if (type == "newSponsorEvent" && snippet.has("newSponsorDetails")) {
        auto d = snippet["newSponsorDetails"];
        std::string level = d.has("memberLevelName") ? std::string(d["memberLevelName"].s()) : "";
        on_event("youtube_sub", author, level);
    } else if (type == "memberMilestoneChatEvent" && snippet.has("memberMilestoneChatDetails")) {
        auto d = snippet["memberMilestoneChatDetails"];
        std::string months = d.has("memberMonth") ? std::to_string(static_cast<int>(d["memberMonth"].i())) : "";
        std::string level = d.has("memberLevelName") ? std::string(d["memberLevelName"].s()) : "";
        on_event("youtube_sub_milestone", author, months + " мес. — " + level);
    } else if (type == "membershipGiftingEvent" && snippet.has("membershipGiftingDetails")) {
        auto d = snippet["membershipGiftingDetails"];
        std::string count = d.has("giftMembershipsCount") ? std::to_string(static_cast<int>(d["giftMembershipsCount"].i())) : "?";
        on_event("youtube_gift_sub", author, count);
    } else if (type == "superChatEvent" && snippet.has("superChatDetails")) {
        auto d = snippet["superChatDetails"];
        std::string amount = d.has("amountDisplayString") ? std::string(d["amountDisplayString"].s()) : "";
        std::string comment = d.has("userComment") ? std::string(d["userComment"].s()) : "";
        on_event("youtube_superchat", author, amount + (comment.empty() ? "" : (" — " + comment)));
    } else if (type == "superStickerEvent" && snippet.has("superStickerDetails")) {
        auto d = snippet["superStickerDetails"];
        std::string amount = d.has("amountDisplayString") ? std::string(d["amountDisplayString"].s()) : "";
        on_event("youtube_supersticker", author, amount);
    }
}

inline void poll_messages(httplib::Client& api, const std::string& live_chat_id, const std::string& api_key,
                           const ChatCallback& on_message, const EventCallback& on_event = nullptr) {
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
                std::string author = (item.has("authorDetails") && item["authorDetails"].has("displayName"))
                                          ? std::string(item["authorDetails"]["displayName"].s())
                                          : "???";
                std::string type = (item.has("snippet") && item["snippet"].has("type"))
                                        ? std::string(item["snippet"]["type"].s())
                                        : "";

                if (type.empty() || type == "textMessageEvent") {
                    std::string text = (item.has("snippet") && item["snippet"].has("displayMessage"))
                                            ? std::string(item["snippet"]["displayMessage"].s())
                                            : "";
                    if (!text.empty()) on_message(author, text);
                } else {
                    route_special_event(item, author, on_event);
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

// video_id is the resolved id of whichever broadcast is (or just stopped
// being) live — empty on the "false" (went offline) call. Lets callers that
// need to act on the actual live broadcast (e.g. updating its title) avoid
// re-resolving it themselves.
using LiveChangeCallback = std::function<void(bool live, const std::string& video_id)>;

// video_id, if set, is used as-is (a specific broadcast, same behavior as
// always). Otherwise the live broadcast is resolved dynamically — via the
// authoritative OAuth check_live_video_id() when oauth_client_id/secret are
// given (instant, no lag), falling back to the public-search-based
// resolve_live_video_id() otherwise (channel_id required in that case) — so
// a new broadcast is picked up automatically once the previous one ends
// without anyone having to touch settings between streams.
//
// on_live_change, if given, fires only on actual transitions (found a live
// chat -> lost it), not on every 15s retry — that's what drives the app's
// "Трансляция запущена/завершена" corner notification.
inline void watch_youtube(const std::string& video_id, const std::string& channel_id, const std::string& api_key,
                           const ChatCallback& on_message, const LiveChangeCallback& on_live_change = nullptr,
                           const std::string& oauth_client_id = "", const std::string& oauth_client_secret = "",
                           const EventCallback& on_event = nullptr) {
    auto api = streamsoft::twitch::make_https_client(kApiHost);
    api.set_read_timeout(20);

    bool has_oauth = !oauth_client_id.empty() && !oauth_client_secret.empty();

    bool was_live = false;
    while (true) {
        try {
            std::string vid = video_id;
            if (vid.empty() && has_oauth) {
                try {
                    std::string token = youtube_auth::get_access_token(oauth_client_id, oauth_client_secret);
                    vid = youtube_auth::check_live_video_id(token);
                    if (vid.empty()) throw std::runtime_error("У канала сейчас нет активной трансляции");
                    CROW_LOG_INFO << "YouTube: нашёл активную трансляцию через OAuth " << vid;
                } catch (const std::exception& e) {
                    if (channel_id.empty()) throw;
                    CROW_LOG_WARNING << "YouTube: OAuth-проверка эфира не удалась (" << e.what() << "), пробую поиск";
                }
            }
            if (vid.empty()) {
                if (channel_id.empty()) {
                    throw std::runtime_error("Не задан ни ID видео, ни ID канала YouTube");
                }
                vid = resolve_live_video_id(api, channel_id, api_key);
                CROW_LOG_INFO << "YouTube: нашёл активную трансляцию " << vid;
            }
            std::string live_chat_id = get_live_chat_id(api, vid, api_key);
            CROW_LOG_INFO << "YouTube чат подключён (liveChatId=" << live_chat_id << ")";
            if (!was_live) {
                was_live = true;
                if (on_live_change) on_live_change(true, vid);
            }
            poll_messages(api, live_chat_id, api_key, on_message, on_event);
        } catch (const std::exception& e) {
            if (was_live) {
                was_live = false;
                if (on_live_change) on_live_change(false, "");
            }
            CROW_LOG_ERROR << "Ошибка YouTube чата, повтор через 15 секунд: " << e.what();
            std::this_thread::sleep_for(std::chrono::seconds(15));
        }
    }
}

}
