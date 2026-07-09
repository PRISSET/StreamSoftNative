import asyncio
import logging

import aiohttp

from models import ChatMessage

log = logging.getLogger("youtube")

API_BASE = "https://www.googleapis.com/youtube/v3"


async def _get_live_chat_id(session: aiohttp.ClientSession, video_id: str, api_key: str) -> str:
    async with session.get(
        f"{API_BASE}/videos",
        params={"part": "liveStreamingDetails", "id": video_id, "key": api_key},
    ) as resp:
        data = await resp.json()

    items = data.get("items", [])
    if not items:
        raise RuntimeError(f"Видео {video_id} не найдено или недоступно (проверь ID и API key)")

    live_chat_id = items[0].get("liveStreamingDetails", {}).get("activeLiveChatId")
    if not live_chat_id:
        raise RuntimeError(f"У видео {video_id} нет активного чата (стрим сейчас не идёт?)")
    return live_chat_id


async def _poll_messages(session: aiohttp.ClientSession, live_chat_id: str, api_key: str, queue: asyncio.Queue) -> None:
    page_token = None
    while True:
        params = {"liveChatId": live_chat_id, "part": "snippet,authorDetails", "key": api_key}
        if page_token:
            params["pageToken"] = page_token

        async with session.get(f"{API_BASE}/liveChat/messages", params=params) as resp:
            if resp.status != 200:
                body = await resp.text()
                raise RuntimeError(f"YouTube API вернул {resp.status}: {body}")
            data = await resp.json()

        for item in data.get("items", []):
            text = item.get("snippet", {}).get("displayMessage", "")
            author = item.get("authorDetails", {}).get("displayName", "???")
            if text:
                await queue.put(ChatMessage(platform="youtube", author=author, text=text))

        page_token = data.get("nextPageToken")
        interval_ms = data.get("pollingIntervalMillis", 5000)
        await asyncio.sleep(max(interval_ms, 2000) / 1000)


async def watch_youtube(video_id: str, api_key: str, queue: asyncio.Queue) -> None:
    async with aiohttp.ClientSession() as session:
        while True:
            try:
                live_chat_id = await _get_live_chat_id(session, video_id, api_key)
                log.info("YouTube чат подключён (liveChatId=%s)", live_chat_id)
                await _poll_messages(session, live_chat_id, api_key, queue)
            except Exception:
                log.exception("Ошибка YouTube чата, повтор через 15 секунд")
                await asyncio.sleep(15)
