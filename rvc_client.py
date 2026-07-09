import logging

import aiohttp

log = logging.getLogger("rvc")


async def is_available(session: aiohttp.ClientSession, base_url: str) -> bool:
    try:
        async with session.get(f"{base_url}/health", timeout=aiohttp.ClientTimeout(total=2)) as resp:
            return resp.status == 200
    except Exception:
        return False


async def list_models(session: aiohttp.ClientSession, base_url: str) -> list:
    try:
        async with session.get(f"{base_url}/models", timeout=aiohttp.ClientTimeout(total=2)) as resp:
            if resp.status != 200:
                return []
            data = await resp.json()
            return data.get("models", [])
    except Exception:
        return []


async def convert(
    session: aiohttp.ClientSession,
    base_url: str,
    audio_bytes: bytes,
    pitch: int,
    index_rate: float,
    protect: float,
    f0method: str,
) -> bytes:
    params = {
        "pitch": str(pitch),
        "index_rate": str(index_rate),
        "protect": str(protect),
        "f0method": f0method,
    }
    async with session.post(
        f"{base_url}/convert",
        params=params,
        data=audio_bytes,
        timeout=aiohttp.ClientTimeout(total=15),
    ) as resp:
        if resp.status != 200:
            body = await resp.text()
            raise RuntimeError(f"RVC-сервис вернул {resp.status}: {body}")
        return await resp.read()
