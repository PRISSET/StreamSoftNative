import asyncio
import json
import logging
import time
from pathlib import Path
from typing import Optional

import aiohttp

log = logging.getLogger("twitch")

TOKEN_FILE = Path(__file__).parent / "twitch_token.json"
AUTH_BASE = "https://id.twitch.tv/oauth2"
SCOPES = "chat:read chat:edit moderator:read:followers channel:read:subscriptions bits:read"

_token_lock = asyncio.Lock()


async def _device_code_flow(session: aiohttp.ClientSession, client_id: str) -> dict:
    async with session.post(
        f"{AUTH_BASE}/device",
        data={"client_id": client_id, "scopes": SCOPES},
    ) as resp:
        data = await resp.json()
        if resp.status != 200:
            raise RuntimeError(f"Не удалось начать авторизацию Twitch: {data}")

    log.info("=== Авторизация Twitch ===")
    log.info("Откройте в браузере: %s", data["verification_uri"])
    log.info("И введите код: %s", data["user_code"])
    log.info("Ожидание подтверждения...")

    interval = data.get("interval", 5)
    device_code = data["device_code"]
    deadline = time.time() + data.get("expires_in", 1800)

    while time.time() < deadline:
        await asyncio.sleep(interval)
        async with session.post(
            f"{AUTH_BASE}/token",
            data={
                "client_id": client_id,
                "scopes": SCOPES,
                "device_code": device_code,
                "grant_type": "urn:ietf:params:oauth:grant-type:device_code",
            },
        ) as resp:
            payload = await resp.json()
        if resp.status == 200:
            return payload
        if payload.get("message") not in ("authorization_pending", "slow_down"):
            raise RuntimeError(f"Ошибка авторизации Twitch: {payload}")

    raise RuntimeError("Время авторизации Twitch истекло, запустите программу снова")


async def _refresh_token(session: aiohttp.ClientSession, client_id: str, refresh_token: str) -> dict:
    async with session.post(
        f"{AUTH_BASE}/token",
        data={"client_id": client_id, "refresh_token": refresh_token, "grant_type": "refresh_token"},
    ) as resp:
        payload = await resp.json()
        if resp.status != 200:
            raise RuntimeError(f"Не удалось обновить токен Twitch: {payload}")
        return payload


def _load_cached() -> Optional[dict]:
    if TOKEN_FILE.exists():
        return json.loads(TOKEN_FILE.read_text())
    return None


def _save(token_data: dict) -> None:
    token_data = dict(token_data)
    token_data["obtained_at"] = time.time()
    TOKEN_FILE.write_text(json.dumps(token_data))


def _has_required_scopes(cached: dict) -> bool:
    granted = set(cached.get("scope") or [])
    required = set(SCOPES.split())
    return required.issubset(granted)


async def get_access_token(client_id: str) -> str:
    async with _token_lock:
        cached = _load_cached()
        if cached and not _has_required_scopes(cached):
            log.info("У сохранённого токена не хватает нужных прав — потребуется повторная авторизация Twitch")
            cached = None

        async with aiohttp.ClientSession() as session:
            if cached:
                obtained_at = cached.get("obtained_at", 0)
                expires_in = cached.get("expires_in", 0)
                if time.time() < obtained_at + expires_in - 60:
                    return cached["access_token"]
                try:
                    refreshed = await _refresh_token(session, client_id, cached["refresh_token"])
                    _save(refreshed)
                    return refreshed["access_token"]
                except Exception:
                    pass

            token_data = await _device_code_flow(session, client_id)
            _save(token_data)
            return token_data["access_token"]


async def get_username(client_id: str, access_token: str) -> str:
    async with aiohttp.ClientSession() as session:
        async with session.get(
            "https://api.twitch.tv/helix/users",
            headers={"Client-Id": client_id, "Authorization": f"Bearer {access_token}"},
        ) as resp:
            data = await resp.json()
            if resp.status != 200:
                raise RuntimeError(f"Не удалось получить имя пользователя Twitch: {data}")
    return data["data"][0]["login"]


async def get_user_id(session: aiohttp.ClientSession, client_id: str, access_token: str, login: str) -> str:
    async with session.get(
        "https://api.twitch.tv/helix/users",
        params={"login": login},
        headers={"Client-Id": client_id, "Authorization": f"Bearer {access_token}"},
    ) as resp:
        data = await resp.json()
        if resp.status != 200 or not data.get("data"):
            raise RuntimeError(f"Не удалось получить id канала {login}: {data}")
    return data["data"][0]["id"]
