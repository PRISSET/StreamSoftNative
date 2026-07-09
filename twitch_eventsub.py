import asyncio
import logging

import aiohttp

from models import StreamEvent
from twitch_auth import get_access_token, get_user_id

log = logging.getLogger("twitch-events")

EVENTSUB_WS_URL = "wss://eventsub.wss.twitch.tv/ws"
HELIX_BASE = "https://api.twitch.tv/helix"

SUBSCRIPTION_SPECS = [
    ("channel.follow", "2", lambda bid: {"broadcaster_user_id": bid, "moderator_user_id": bid}),
    ("channel.subscribe", "1", lambda bid: {"broadcaster_user_id": bid}),
    ("channel.subscription.gift", "1", lambda bid: {"broadcaster_user_id": bid}),
    ("channel.raid", "1", lambda bid: {"to_broadcaster_user_id": bid}),
    ("channel.cheer", "1", lambda bid: {"broadcaster_user_id": bid}),
]


async def _subscribe(
    session: aiohttp.ClientSession,
    client_id: str,
    token: str,
    sub_type: str,
    version: str,
    condition: dict,
    session_id: str,
) -> None:
    body = {
        "type": sub_type,
        "version": version,
        "condition": condition,
        "transport": {"method": "websocket", "session_id": session_id},
    }
    async with session.post(
        f"{HELIX_BASE}/eventsub/subscriptions",
        json=body,
        headers={"Client-Id": client_id, "Authorization": f"Bearer {token}"},
    ) as resp:
        data = await resp.json()
        if resp.status not in (200, 202):
            log.warning(
                "Не удалось подписаться на событие %s (%s) — возможно не хватает прав/скоупа: %s",
                sub_type, resp.status, data,
            )
            return
    log.info("Подписка на событие Twitch %s оформлена", sub_type)


def _extract_stream_event(sub_type: str, event: dict) -> StreamEvent:
    if sub_type == "channel.follow":
        return StreamEvent(kind="follow", user=event.get("user_name", "???"))

    if sub_type == "channel.subscribe":
        tier = str(event.get("tier", "1000"))[:1]
        return StreamEvent(kind="subscribe", user=event.get("user_name", "???"), detail=f"уровень {tier}")

    if sub_type == "channel.subscription.gift":
        total = event.get("total", 1)
        gifter = event.get("user_name") or "Аноним"
        return StreamEvent(kind="gift_sub", user=gifter, detail=f"{total} шт.")

    if sub_type == "channel.raid":
        viewers = event.get("viewers", 0)
        return StreamEvent(kind="raid", user=event.get("from_broadcaster_user_name", "???"), detail=f"{viewers} зрителей")

    if sub_type == "channel.cheer":
        bits = event.get("bits", 0)
        user = event.get("user_name") or "Аноним"
        return StreamEvent(kind="cheer", user=user, detail=f"{bits} bits")

    return StreamEvent(kind=sub_type, user=event.get("user_name", "???"))


async def _run_once(channel: str, client_id: str, queue: asyncio.Queue) -> None:
    access_token = await get_access_token(client_id)

    async with aiohttp.ClientSession() as session:
        broadcaster_id = await get_user_id(session, client_id, access_token, channel)

        ws_url = EVENTSUB_WS_URL
        already_subscribed = False

        while True:
            async with session.ws_connect(ws_url, heartbeat=30) as ws:
                reconnect_url = None

                async for msg in ws:
                    if msg.type != aiohttp.WSMsgType.TEXT:
                        continue
                    payload = msg.json()
                    msg_type = payload.get("metadata", {}).get("message_type")

                    if msg_type == "session_welcome":
                        session_id = payload["payload"]["session"]["id"]
                        log.info("Twitch EventSub подключён (session_id=%s)", session_id)
                        if not already_subscribed:
                            for sub_type, version, cond_fn in SUBSCRIPTION_SPECS:
                                await _subscribe(
                                    session, client_id, access_token, sub_type, version,
                                    cond_fn(broadcaster_id), session_id,
                                )
                            already_subscribed = True

                    elif msg_type == "session_keepalive":
                        continue

                    elif msg_type == "notification":
                        sub_type = payload["payload"]["subscription"]["type"]
                        event = payload["payload"]["event"]
                        stream_event = _extract_stream_event(sub_type, event)
                        log.info("Событие Twitch: %s (%s) %s", stream_event.kind, stream_event.user, stream_event.detail)
                        await queue.put(stream_event)

                    elif msg_type == "session_reconnect":
                        reconnect_url = payload["payload"]["session"]["reconnect_url"]
                        log.info("Twitch запросил переподключение EventSub")
                        break

                    elif msg_type == "revocation":
                        log.warning("Twitch отозвал подписку EventSub: %s", payload["payload"])

                if reconnect_url:
                    ws_url = reconnect_url
                    continue

                raise ConnectionError("Twitch EventSub WebSocket закрыт сервером")


async def watch_twitch_events(channel: str, client_id: str, queue: asyncio.Queue) -> None:
    while True:
        try:
            await _run_once(channel, client_id, queue)
        except Exception:
            log.exception("Ошибка Twitch EventSub, повтор через 20 секунд")
            await asyncio.sleep(20)
