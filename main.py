import asyncio
import logging
import sys
from typing import Optional

import aiohttp

import rvc_client
import rvc_launcher
from chat_commands import CommandsStore
from config import load_settings
from models import ChatMessage, StreamEvent
from moderation import ModerationState
from overlay_server import OverlayServer, run_overlay_server
from runtime_settings import RuntimeSettings
from telegram_control import watch_telegram_commands
from telegram_notify import send_telegram_event, send_telegram_message
from tts_worker import TtsWorker
from twitch_chat import watch_twitch
from twitch_eventsub import watch_twitch_events
from youtube_chat import watch_youtube

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(name)s] %(message)s")
log = logging.getLogger("main")

EVENT_SPEECH_TEMPLATES = {
    "follow": "{user} подписался на канал",
    "subscribe": "{user} оформил подписку, {detail}",
    "gift_sub": "{user} подарил подписки, {detail}",
    "raid": "Рейд от {user}, {detail}",
    "cheer": "{user} задонатил битсами, {detail}",
}


def _event_speech(evt: StreamEvent) -> str:
    template = EVENT_SPEECH_TEMPLATES.get(evt.kind, "{user}: событие {kind}")
    return template.format(user=evt.user, detail=evt.detail, kind=evt.kind)


async def consume(
    queue: "asyncio.Queue",
    tts: TtsWorker,
    moderation: ModerationState,
    overlay: Optional[OverlayServer],
    bot_token: str,
    chat_id: str,
    commands: CommandsStore,
    twitch_outgoing: Optional[asyncio.Queue],
) -> None:
    async with aiohttp.ClientSession() as session:
        while True:
            item = await queue.get()
            try:
                await _consume_one(item, tts, moderation, overlay, session, bot_token, chat_id, commands, twitch_outgoing)
            except Exception:
                log.exception("Не удалось обработать сообщение/событие, пропускаю: %r", item)


async def _consume_one(
    item,
    tts: TtsWorker,
    moderation: ModerationState,
    overlay: Optional[OverlayServer],
    session: aiohttp.ClientSession,
    bot_token: str,
    chat_id: str,
    commands: CommandsStore,
    twitch_outgoing: Optional[asyncio.Queue],
) -> None:
    if isinstance(item, ChatMessage):
        if moderation.is_muted(item.author):
            return
        log.info("[%s] %s: %s", item.platform, item.author, item.text)
        tts.say(item.author, item.text)
        if overlay:
            try:
                await overlay.broadcast(
                    {"type": "chat", "platform": item.platform, "author": item.author, "text": item.text}
                )
            except Exception:
                log.exception("Не удалось разослать сообщение чата в оверлей")
        try:
            await send_telegram_message(session, bot_token, chat_id, item.platform, item.author, item.text)
        except Exception:
            log.exception("Не удалось отправить сообщение в Telegram")

        reply = commands.match(item.text)
        if reply and item.platform == "twitch" and twitch_outgoing:
            await twitch_outgoing.put(reply)

    elif isinstance(item, StreamEvent):
        log.info("[event] %s: %s %s", item.kind, item.user, item.detail)
        tts.say_event(_event_speech(item))
        if overlay:
            try:
                await overlay.broadcast(
                    {"type": "event", "kind": item.kind, "user": item.user, "detail": item.detail}
                )
            except Exception:
                log.exception("Не удалось разослать событие в оверлей")
        try:
            await send_telegram_event(session, bot_token, chat_id, item)
        except Exception:
            log.exception("Не удалось отправить событие в Telegram")


async def _supervise(name: str, coro_factory, restart_delay: float = 5.0) -> None:
    while True:
        try:
            await coro_factory()
        except asyncio.CancelledError:
            raise
        except Exception:
            log.exception("Задача '%s' упала, перезапуск через %.0f сек", name, restart_delay)
            await asyncio.sleep(restart_delay)


async def main() -> None:
    settings = load_settings()

    runtime = RuntimeSettings.load(
        RuntimeSettings(
            tts_voice_ru=settings.tts_voice_ru,
            tts_voice_en=settings.tts_voice_en,
            tts_rate=settings.tts_rate,
            tts_volume=settings.tts_volume,
            tts_say_author=settings.tts_say_author,
        )
    )

    tts = TtsWorker(
        settings.tts_voice,
        runtime.tts_voice_ru,
        runtime.tts_voice_en,
        runtime.tts_rate,
        runtime.tts_say_author,
        settings.tts_max_chars,
        runtime.tts_volume,
        rvc_enabled=runtime.rvc_enabled,
        rvc_base_url=runtime.rvc_base_url,
        rvc_scope=runtime.rvc_scope,
        rvc_pitch=runtime.rvc_pitch,
        rvc_index_rate=runtime.rvc_index_rate,
        rvc_protect=runtime.rvc_protect,
        rvc_f0method=runtime.rvc_f0method,
    )
    tts.start()

    rvc_process = None
    if settings.rvc_autostart and rvc_launcher.is_installed():
        async with aiohttp.ClientSession() as session:
            already_running = await rvc_client.is_available(session, runtime.rvc_base_url)
        if already_running:
            log.info("RVC-сервис уже запущен, использую существующий")
        else:
            rvc_process = rvc_launcher.start(runtime.rvc_base_url)

    moderation = ModerationState()
    commands = CommandsStore()
    twitch_outgoing: Optional[asyncio.Queue] = asyncio.Queue() if settings.twitch_channel else None
    overlay = (
        OverlayServer(settings.overlay_port, tts, moderation, runtime, commands) if settings.overlay_enabled else None
    )

    queue: asyncio.Queue = asyncio.Queue()
    tasks = [
        asyncio.create_task(
            _supervise(
                "consume",
                lambda: consume(
                    queue,
                    tts,
                    moderation,
                    overlay,
                    settings.telegram_bot_token,
                    settings.telegram_chat_id,
                    commands,
                    twitch_outgoing,
                ),
            )
        )
    ]

    if overlay:
        tasks.append(asyncio.create_task(_supervise("overlay", lambda: run_overlay_server(overlay))))

    if settings.telegram_control_enabled:
        tasks.append(
            asyncio.create_task(
                _supervise(
                    "telegram-control",
                    lambda: watch_telegram_commands(
                        settings.telegram_bot_token, settings.telegram_chat_id, moderation, tts
                    ),
                )
            )
        )

    if settings.youtube_video_id:
        tasks.append(
            asyncio.create_task(
                _supervise("youtube", lambda: watch_youtube(settings.youtube_video_id, settings.youtube_api_key, queue))
            )
        )
    else:
        log.warning("YOUTUBE_VIDEO_ID не задан — YouTube чат отключён")

    if settings.twitch_channel:
        tasks.append(
            asyncio.create_task(
                _supervise(
                    "twitch-chat",
                    lambda: watch_twitch(settings.twitch_channel, settings.twitch_client_id, queue, twitch_outgoing),
                )
            )
        )
        if settings.twitch_eventsub_enabled:
            tasks.append(
                asyncio.create_task(
                    _supervise(
                        "twitch-eventsub",
                        lambda: watch_twitch_events(settings.twitch_channel, settings.twitch_client_id, queue),
                    )
                )
            )
    else:
        log.warning("TWITCH_CHANNEL не задан — Twitch чат отключён")

    try:
        await asyncio.gather(*tasks)
    finally:
        rvc_launcher.stop(rvc_process)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
