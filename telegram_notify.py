import logging

import aiohttp

from models import StreamEvent

log = logging.getLogger("telegram")

PLATFORM_LABELS = {
    "youtube": "🔴 YouTube",
    "twitch": "💜 Twitch",
}

EVENT_LABELS = {
    "follow": "💚 Новый фоллоу",
    "subscribe": "⭐ Новая подписка",
    "gift_sub": "🎁 Подарочная подписка",
    "raid": "🚀 Рейд",
    "cheer": "💎 Донат битсами",
}


async def send_telegram_message(
    session: aiohttp.ClientSession, bot_token: str, chat_id: str, platform: str, author: str, text: str
) -> None:
    label = PLATFORM_LABELS.get(platform, platform)
    message = f"{label} | {author}:\n{text}"
    url = f"https://api.telegram.org/bot{bot_token}/sendMessage"

    async with session.post(url, data={"chat_id": chat_id, "text": message}) as resp:
        if resp.status != 200:
            body = await resp.text()
            log.error("Telegram API ошибка %s: %s", resp.status, body)


async def send_telegram_event(
    session: aiohttp.ClientSession, bot_token: str, chat_id: str, event: StreamEvent
) -> None:
    label = EVENT_LABELS.get(event.kind, event.kind)
    message = f"{label}\n{event.user}" + (f" — {event.detail}" if event.detail else "")
    url = f"https://api.telegram.org/bot{bot_token}/sendMessage"

    async with session.post(url, data={"chat_id": chat_id, "text": message}) as resp:
        if resp.status != 200:
            body = await resp.text()
            log.error("Telegram API ошибка %s: %s", resp.status, body)
