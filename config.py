import os
import re
from dataclasses import dataclass

from dotenv import load_dotenv

load_dotenv()


@dataclass(frozen=True)
class Settings:
    youtube_api_key: str
    youtube_video_id: str
    twitch_client_id: str
    twitch_channel: str
    twitch_eventsub_enabled: bool
    telegram_bot_token: str
    telegram_chat_id: str
    telegram_control_enabled: bool
    tts_voice: str
    tts_voice_ru: str
    tts_voice_en: str
    tts_rate: str
    tts_say_author: bool
    tts_max_chars: int
    tts_volume: int
    overlay_enabled: bool
    overlay_port: int
    rvc_autostart: bool


def _env(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


def _bool_env(name: str, default: bool) -> bool:
    value = _env(name, "").lower()
    if not value:
        return default
    return value in ("true", "1", "yes", "on")


def _extract_youtube_id(value: str) -> str:
    if not value:
        return value
    match = re.search(r"(?:v=|youtu\.be/|/live/)([A-Za-z0-9_-]{11})", value)
    return match.group(1) if match else value


def load_settings() -> Settings:
    telegram_bot_token = _env("TELEGRAM_BOT_TOKEN")
    telegram_chat_id = _env("TELEGRAM_CHAT_ID")
    if not telegram_bot_token or not telegram_chat_id:
        raise RuntimeError("В .env должны быть заданы TELEGRAM_BOT_TOKEN и TELEGRAM_CHAT_ID")

    youtube_video_id = _extract_youtube_id(_env("YOUTUBE_VIDEO_ID"))
    if youtube_video_id and not _env("YOUTUBE_API_KEY"):
        raise RuntimeError("Задан YOUTUBE_VIDEO_ID, но не задан YOUTUBE_API_KEY")

    twitch_channel = _env("TWITCH_CHANNEL").lower().lstrip("#")
    if twitch_channel and not _env("TWITCH_CLIENT_ID"):
        raise RuntimeError("Задан TWITCH_CHANNEL, но не задан TWITCH_CLIENT_ID")

    if not youtube_video_id and not twitch_channel:
        raise RuntimeError("Нужно задать хотя бы один источник чата: YOUTUBE_VIDEO_ID или TWITCH_CHANNEL")

    tts_voice = _env("TTS_VOICE", "ru-RU-DmitryNeural")

    return Settings(
        youtube_api_key=_env("YOUTUBE_API_KEY"),
        youtube_video_id=youtube_video_id,
        twitch_client_id=_env("TWITCH_CLIENT_ID"),
        twitch_channel=twitch_channel,
        twitch_eventsub_enabled=_bool_env("TWITCH_EVENTSUB_ENABLED", True),
        telegram_bot_token=telegram_bot_token,
        telegram_chat_id=telegram_chat_id,
        telegram_control_enabled=_bool_env("TELEGRAM_CONTROL_ENABLED", True),
        tts_voice=tts_voice,
        tts_voice_ru=_env("TTS_VOICE_RU", tts_voice),
        tts_voice_en=_env("TTS_VOICE_EN", "en-US-GuyNeural"),
        tts_rate=_env("TTS_RATE", "+0%"),
        tts_say_author=_bool_env("TTS_SAY_AUTHOR", True),
        tts_max_chars=int(_env("TTS_MAX_CHARS", "200")),
        tts_volume=int(_env("TTS_VOLUME", "100")),
        overlay_enabled=_bool_env("OVERLAY_ENABLED", True),
        overlay_port=int(_env("OVERLAY_PORT", "8099")),
        rvc_autostart=_bool_env("RVC_AUTOSTART", True),
    )
