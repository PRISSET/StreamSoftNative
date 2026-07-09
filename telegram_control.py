import asyncio
import logging

import aiohttp

from moderation import ModerationState
from tts_worker import TtsWorker

log = logging.getLogger("telegram-control")

HELP_TEXT = (
    "Команды:\n"
    "/mute <ник> — не читать и не пересылать сообщения ника\n"
    "/unmute <ник> — снять мьют\n"
    "/muted — список замьюченных\n"
    "/skip — прервать текущую озвучку и очистить очередь\n"
    "/volume <0-200> — громкость TTS (100 = обычная)"
)


async def _reply(session: aiohttp.ClientSession, base: str, chat_id: str, text: str) -> None:
    async with session.post(f"{base}/sendMessage", data={"chat_id": chat_id, "text": text}):
        pass


async def _handle_command(
    session: aiohttp.ClientSession,
    base: str,
    chat_id: str,
    text: str,
    moderation: ModerationState,
    tts: TtsWorker,
) -> None:
    parts = text.split(maxsplit=1)
    command = parts[0].lower().split("@")[0]
    arg = parts[1].strip() if len(parts) > 1 else ""

    if command == "/mute":
        if not arg:
            await _reply(session, base, chat_id, "Использование: /mute <ник>")
            return
        moderation.mute(arg)
        await _reply(session, base, chat_id, f"🔇 {arg} замьючен (чат не читается и не пересылается)")

    elif command == "/unmute":
        if not arg:
            await _reply(session, base, chat_id, "Использование: /unmute <ник>")
            return
        moderation.unmute(arg)
        await _reply(session, base, chat_id, f"🔊 {arg} размьючен")

    elif command == "/muted":
        names = moderation.list_muted()
        await _reply(session, base, chat_id, "Замьючены: " + (", ".join(names) if names else "никто"))

    elif command == "/skip":
        stopped = tts.skip_current()
        cleared = tts.clear_queue()
        await _reply(
            session,
            base,
            chat_id,
            f"⏭ Прервано текущее: {'да' if stopped else 'нет'}. Убрано из очереди: {cleared}",
        )

    elif command == "/volume":
        if not arg.isdigit():
            await _reply(session, base, chat_id, "Использование: /volume <0-200>")
            return
        percent = int(arg)
        tts.set_volume_percent(percent)
        await _reply(session, base, chat_id, f"🔊 Громкость: {percent}%")

    elif command in ("/help", "/start"):
        await _reply(session, base, chat_id, HELP_TEXT)


async def watch_telegram_commands(
    bot_token: str, admin_chat_id: str, moderation: ModerationState, tts: TtsWorker
) -> None:
    base = f"https://api.telegram.org/bot{bot_token}"
    offset = 0

    async with aiohttp.ClientSession() as session:
        while True:
            try:
                async with session.get(
                    f"{base}/getUpdates",
                    params={"offset": offset, "timeout": 25},
                    timeout=aiohttp.ClientTimeout(total=35),
                ) as resp:
                    data = await resp.json()
            except Exception:
                log.exception("Ошибка опроса команд Telegram, повтор через 5 секунд")
                await asyncio.sleep(5)
                continue

            for update in data.get("result", []):
                offset = update["update_id"] + 1
                message = update.get("message") or update.get("channel_post")
                if not message:
                    continue
                chat_id = str(message.get("chat", {}).get("id", ""))
                if chat_id != str(admin_chat_id):
                    continue
                text = (message.get("text") or "").strip()
                if not text.startswith("/"):
                    continue
                try:
                    await _handle_command(session, base, chat_id, text, moderation, tts)
                except Exception:
                    log.exception("Ошибка обработки команды: %s", text)
