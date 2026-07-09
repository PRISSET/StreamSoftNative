import asyncio
import logging
import re
from typing import Optional

from models import ChatMessage
from twitch_auth import get_access_token, get_username

log = logging.getLogger("twitch")

IRC_HOST = "irc.chat.twitch.tv"
IRC_PORT = 6697

PRIVMSG_RE = re.compile(r"^:(?P<user>[^!]+)!\S+\s+PRIVMSG\s+#\S+\s+:(?P<message>.*)$")


async def _connect_and_listen(
    channel: str, nick: str, access_token: str, queue: asyncio.Queue, outgoing: Optional[asyncio.Queue]
) -> None:
    reader, writer = await asyncio.open_connection(IRC_HOST, IRC_PORT, ssl=True)

    def send(line: str) -> None:
        writer.write((line + "\r\n").encode("utf-8"))

    send(f"PASS oauth:{access_token}")
    send(f"NICK {nick}")
    send(f"JOIN #{channel}")
    await writer.drain()
    log.info("Отправлен хендшейк Twitch IRC как %s, ждём подтверждения от сервера...", nick)

    async def read_loop() -> None:
        while True:
            line = await reader.readline()
            if not line:
                raise ConnectionError("Twitch IRC соединение закрыто")
            text = line.decode("utf-8", errors="ignore").strip()
            log.debug("IRC << %s", text)

            if text.startswith("PING"):
                send(text.replace("PING", "PONG", 1))
                await writer.drain()
                continue

            if "Login authentication failed" in text or "Improperly formatted auth" in text:
                raise RuntimeError(f"Twitch отклонил авторизацию: {text}")

            if " JOIN #" in text and text.startswith(f":{nick}!"):
                log.info("Twitch чат подключён (#%s)", channel)
                continue

            match = PRIVMSG_RE.match(text)
            if match:
                await queue.put(
                    ChatMessage(platform="twitch", author=match.group("user"), text=match.group("message"))
                )

    async def write_loop() -> None:
        if outgoing is None:
            return
        while True:
            text = await outgoing.get()
            for line in text.splitlines() or [""]:
                if line:
                    send(f"PRIVMSG #{channel} :{line}")
            await writer.drain()

    await asyncio.gather(read_loop(), write_loop())


async def watch_twitch(
    channel: str, client_id: str, queue: asyncio.Queue, outgoing: Optional[asyncio.Queue] = None
) -> None:
    while True:
        try:
            access_token = await get_access_token(client_id)
            nick = await get_username(client_id, access_token)
            await _connect_and_listen(channel, nick, access_token, queue, outgoing)
        except Exception:
            log.exception("Ошибка Twitch чата, повтор через 15 секунд")
            await asyncio.sleep(15)
