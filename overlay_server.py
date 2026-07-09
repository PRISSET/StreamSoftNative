import asyncio
import json
import logging
from collections import deque
from pathlib import Path
from typing import Optional

import aiohttp
from aiohttp import web

import rvc_client
from chat_commands import CommandsStore
from moderation import ModerationState
from runtime_settings import RuntimeSettings
from tts_worker import TtsWorker

log = logging.getLogger("overlay")

STATIC_DIR = Path(__file__).parent / "overlay"
MEDIA_DIR = STATIC_DIR / "media"
CHAT_HISTORY_SIZE = 30
EVENT_KINDS = ("follow", "subscribe", "gift_sub", "raid", "cheer")
MEDIA_EXTS = ("gif", "mp3")

@web.middleware
async def _no_cache_static(request: web.Request, handler):
    response = await handler(request)
    if request.path.startswith("/static/"):
        response.headers["Cache-Control"] = "no-cache, must-revalidate"
    return response


TEST_EVENT_SAMPLES = {
    "follow": {"user": "TestUser", "detail": ""},
    "subscribe": {"user": "TestUser", "detail": "уровень 1"},
    "gift_sub": {"user": "TestUser", "detail": "3 шт."},
    "raid": {"user": "TestUser", "detail": "42 зрителей"},
    "cheer": {"user": "TestUser", "detail": "100 bits"},
}


class OverlayServer:
    def __init__(
        self,
        port: int,
        tts: TtsWorker,
        moderation: ModerationState,
        runtime: RuntimeSettings,
        commands: CommandsStore,
    ):
        self._port = port
        self._tts = tts
        self._moderation = moderation
        self._runtime = runtime
        self._commands = commands
        self._clients: set[web.WebSocketResponse] = set()
        self._chat_history: "deque[dict]" = deque(maxlen=CHAT_HISTORY_SIZE)

        MEDIA_DIR.mkdir(exist_ok=True)

        self._app = web.Application(client_max_size=32 * 1024 * 1024, middlewares=[_no_cache_static])
        self._app.router.add_get("/", self._page("index.html"))
        self._app.router.add_get("/chat", self._page("chat.html"))
        self._app.router.add_get("/events", self._page("events.html"))
        self._app.router.add_get("/settings", self._page("settings.html"))
        self._app.router.add_get("/ws", self._handle_ws)
        self._app.router.add_get("/api/settings", self._get_settings)
        self._app.router.add_post("/api/settings", self._post_settings)
        self._app.router.add_get("/api/media/status", self._media_status)
        self._app.router.add_post("/api/media/{kind}/{ext}", self._upload_media)
        self._app.router.add_delete("/api/media/{kind}/{ext}", self._delete_media)
        self._app.router.add_post("/api/test-event", self._test_event)
        self._app.router.add_post("/api/test-chat", self._test_chat)
        self._app.router.add_get("/api/rvc/health", self._rvc_health)
        self._app.router.add_get("/api/rvc/models", self._rvc_models)
        self._app.router.add_get("/api/commands", self._list_commands)
        self._app.router.add_post("/api/commands", self._add_command)
        self._app.router.add_delete("/api/commands/{trigger}", self._delete_command)
        self._app.router.add_post("/api/commands/{trigger}/toggle", self._toggle_command)
        self._app.router.add_static("/media", MEDIA_DIR)
        self._app.router.add_static("/static", STATIC_DIR)
        self._runner: Optional[web.AppRunner] = None

    def _page(self, filename: str):
        async def handler(request: web.Request) -> web.Response:
            html = (STATIC_DIR / filename).read_text(encoding="utf-8")
            return web.Response(text=html, content_type="text/html")

        return handler

    async def start(self) -> None:
        self._runner = web.AppRunner(self._app)
        await self._runner.setup()
        site = web.TCPSite(self._runner, "127.0.0.1", self._port)
        await site.start()
        log.info(
            "Оверлей: http://127.0.0.1:%d/ (всё вместе), /chat, /events, /settings (панель настроек)",
            self._port,
        )

    def _config_payload(self) -> dict:
        return {
            "type": "config",
            "theme": self._runtime.theme,
            "eventVolume": self._runtime.event_volume / 100,
            "overlayScale": self._runtime.overlay_scale,
        }

    def _settings_payload(self) -> dict:
        return {**self._runtime.as_dict(), "muted": self._moderation.list_muted()}

    async def _handle_ws(self, request: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        self._clients.add(ws)
        await ws.send_str(json.dumps(self._config_payload(), ensure_ascii=False))
        for payload in self._chat_history:
            await ws.send_str(json.dumps(payload, ensure_ascii=False))
        try:
            async for _ in ws:
                pass
        finally:
            self._clients.discard(ws)
        return ws

    async def broadcast(self, payload: dict) -> None:
        if payload.get("type") == "chat":
            self._chat_history.append(payload)

        if not self._clients:
            return
        data = json.dumps(payload, ensure_ascii=False)
        dead = []
        for ws in list(self._clients):
            try:
                await ws.send_str(data)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self._clients.discard(ws)

    async def _broadcast_config(self) -> None:
        await self.broadcast(self._config_payload())

    async def _get_settings(self, request: web.Request) -> web.Response:
        return web.json_response(self._settings_payload())

    async def _post_settings(self, request: web.Request) -> web.Response:
        try:
            body = await request.json()
        except Exception:
            return web.json_response({"ok": False, "error": "bad json"}, status=400)

        if "theme" in body:
            self._runtime.theme = str(body["theme"])
        if "tts_voice_ru" in body:
            self._runtime.tts_voice_ru = str(body["tts_voice_ru"])
            self._tts.set_voice_ru(self._runtime.tts_voice_ru)
        if "tts_voice_en" in body:
            self._runtime.tts_voice_en = str(body["tts_voice_en"])
            self._tts.set_voice_en(self._runtime.tts_voice_en)
        if "tts_rate" in body:
            self._runtime.tts_rate = str(body["tts_rate"])
            self._tts.set_rate(self._runtime.tts_rate)
        if "tts_volume" in body:
            self._runtime.tts_volume = int(body["tts_volume"])
            self._tts.set_volume_percent(self._runtime.tts_volume)
        if "tts_say_author" in body:
            self._runtime.tts_say_author = bool(body["tts_say_author"])
            self._tts.set_say_author(self._runtime.tts_say_author)
        if "event_volume" in body:
            self._runtime.event_volume = int(body["event_volume"])
        if "overlay_scale" in body:
            self._runtime.overlay_scale = max(0.5, min(2.5, float(body["overlay_scale"])))

        if "rvc_enabled" in body:
            self._runtime.rvc_enabled = bool(body["rvc_enabled"])
            self._tts.set_rvc_enabled(self._runtime.rvc_enabled)
        if "rvc_model" in body:
            self._runtime.rvc_model = str(body["rvc_model"])
        if "rvc_scope" in body:
            self._runtime.rvc_scope = str(body["rvc_scope"])
            self._tts.set_rvc_scope(self._runtime.rvc_scope)
        if any(k in body for k in ("rvc_pitch", "rvc_index_rate", "rvc_protect", "rvc_f0method")):
            if "rvc_pitch" in body:
                self._runtime.rvc_pitch = int(body["rvc_pitch"])
            if "rvc_index_rate" in body:
                self._runtime.rvc_index_rate = float(body["rvc_index_rate"])
            if "rvc_protect" in body:
                self._runtime.rvc_protect = float(body["rvc_protect"])
            if "rvc_f0method" in body:
                self._runtime.rvc_f0method = str(body["rvc_f0method"])
            self._tts.set_rvc_params(
                pitch=self._runtime.rvc_pitch,
                index_rate=self._runtime.rvc_index_rate,
                protect=self._runtime.rvc_protect,
                f0method=self._runtime.rvc_f0method,
            )

        if body.get("mute"):
            self._moderation.mute(str(body["mute"]))
        if body.get("unmute"):
            self._moderation.unmute(str(body["unmute"]))

        self._runtime.save()
        await self._broadcast_config()
        return web.json_response({"ok": True, **self._settings_payload()})

    async def _media_status(self, request: web.Request) -> web.Response:
        status = {
            kind: {ext: (MEDIA_DIR / f"{kind}.{ext}").exists() for ext in MEDIA_EXTS} for kind in EVENT_KINDS
        }
        return web.json_response(status)

    async def _upload_media(self, request: web.Request) -> web.Response:
        kind = request.match_info["kind"]
        ext = request.match_info["ext"]
        if kind not in EVENT_KINDS or ext not in MEDIA_EXTS:
            return web.json_response({"ok": False, "error": "bad kind/ext"}, status=400)

        data = await request.read()
        if not data:
            return web.json_response({"ok": False, "error": "empty file"}, status=400)

        (MEDIA_DIR / f"{kind}.{ext}").write_bytes(data)
        return web.json_response({"ok": True})

    async def _delete_media(self, request: web.Request) -> web.Response:
        kind = request.match_info["kind"]
        ext = request.match_info["ext"]
        if kind not in EVENT_KINDS or ext not in MEDIA_EXTS:
            return web.json_response({"ok": False, "error": "bad kind/ext"}, status=400)

        path = MEDIA_DIR / f"{kind}.{ext}"
        if path.exists():
            path.unlink()
        return web.json_response({"ok": True})

    async def _test_event(self, request: web.Request) -> web.Response:
        body = await request.json()
        kind = body.get("kind", "follow")
        sample = TEST_EVENT_SAMPLES.get(kind, {"user": "TestUser", "detail": ""})
        await self.broadcast({"type": "event", "kind": kind, "user": sample["user"], "detail": sample["detail"]})
        self._tts.say_event(f"{sample['user']}: тестовое событие, {sample['detail'] or kind}")
        return web.json_response({"ok": True})

    async def _test_chat(self, request: web.Request) -> web.Response:
        await self.broadcast(
            {"type": "chat", "platform": "twitch", "author": "TestUser", "text": "Тестовое сообщение чата"}
        )
        self._tts.say("TestUser", "Тестовое сообщение чата, проверка голоса.")
        return web.json_response({"ok": True})

    async def _rvc_health(self, request: web.Request) -> web.Response:
        async with aiohttp.ClientSession() as session:
            available = await rvc_client.is_available(session, self._runtime.rvc_base_url)
        return web.json_response({"available": available})

    async def _rvc_models(self, request: web.Request) -> web.Response:
        async with aiohttp.ClientSession() as session:
            models = await rvc_client.list_models(session, self._runtime.rvc_base_url)
        return web.json_response({"models": models})

    async def _list_commands(self, request: web.Request) -> web.Response:
        return web.json_response({"commands": self._commands.list()})

    async def _add_command(self, request: web.Request) -> web.Response:
        try:
            body = await request.json()
        except Exception:
            return web.json_response({"ok": False, "error": "bad json"}, status=400)

        trigger = str(body.get("trigger", "")).strip()
        response = str(body.get("response", "")).strip()
        cooldown = int(body.get("cooldown", 15))
        if not trigger or not response:
            return web.json_response({"ok": False, "error": "trigger и response обязательны"}, status=400)

        self._commands.add(trigger, response, cooldown)
        return web.json_response({"ok": True, "commands": self._commands.list()})

    async def _delete_command(self, request: web.Request) -> web.Response:
        self._commands.remove(request.match_info["trigger"])
        return web.json_response({"ok": True, "commands": self._commands.list()})

    async def _toggle_command(self, request: web.Request) -> web.Response:
        body = await request.json()
        self._commands.set_enabled(request.match_info["trigger"], bool(body.get("enabled", True)))
        return web.json_response({"ok": True, "commands": self._commands.list()})


async def run_overlay_server(overlay: OverlayServer) -> None:
    await overlay.start()
    await asyncio.Event().wait()
