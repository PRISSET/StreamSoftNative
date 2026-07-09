import logging
import subprocess
from pathlib import Path
from typing import Optional
from urllib.parse import urlparse

log = logging.getLogger("rvc-launcher")

RVC_DIR = Path(__file__).parent / "rvc_service"
RVC_PYTHON = RVC_DIR / "venv" / "Scripts" / "python.exe"


def is_installed() -> bool:
    return RVC_PYTHON.exists() and (RVC_DIR / "server.py").exists()


def start(base_url: str) -> Optional[subprocess.Popen]:
    if not is_installed():
        log.info("rvc_service не найден локально (папка rvc_service/venv отсутствует) — автозапуск пропущен")
        return None

    port = urlparse(base_url).port or 8100
    log.info("Запускаю RVC-сервис в фоне на порту %d (модель прогреется 10-20 секунд)...", port)

    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    return subprocess.Popen(
        [str(RVC_PYTHON), "-m", "uvicorn", "server:app", "--host", "127.0.0.1", "--port", str(port)],
        cwd=str(RVC_DIR),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=creationflags,
    )


def stop(process: Optional[subprocess.Popen]) -> None:
    if process and process.poll() is None:
        log.info("Останавливаю RVC-сервис")
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
