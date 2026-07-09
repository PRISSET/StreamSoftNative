import asyncio
import logging
import queue
import re
import subprocess
import sys
import tempfile
import threading
from pathlib import Path
from typing import Optional

import aiohttp
import edge_tts

import rvc_client

log = logging.getLogger("tts")

URL_RE = re.compile(r"https?://\S+")
CYRILLIC_RE = re.compile(r"[а-яА-ЯёЁ]")
LATIN_RE = re.compile(r"[a-zA-Z]")

IS_WINDOWS = sys.platform == "win32"
PLAYER_CMD = {
    "darwin": ["afplay"],
    "linux": ["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet"],
}

if IS_WINDOWS:
    import ctypes

    _winmm = ctypes.windll.winmm

    def _mci(command: str) -> None:
        buf = ctypes.create_unicode_buffer(255)
        error = _winmm.mciSendStringW(command, buf, 254, 0)
        if error:
            raise RuntimeError(f"Ошибка воспроизведения (MCI, код {error}): {command}")


class TtsWorker:
    def __init__(
        self,
        voice_default: str,
        voice_ru: str,
        voice_en: str,
        rate: str,
        say_author: bool,
        max_chars: int,
        volume_percent: int = 100,
        rvc_enabled: bool = False,
        rvc_base_url: str = "http://127.0.0.1:8100",
        rvc_scope: str = "alerts",
        rvc_pitch: int = 12,
        rvc_index_rate: float = 0.3,
        rvc_protect: float = 0.5,
        rvc_f0method: str = "rmvpe",
    ):
        self._voice_default = voice_default
        self._voice_ru = voice_ru
        self._voice_en = voice_en
        self._rate = rate
        self._say_author = say_author
        self._max_chars = max_chars
        self._volume = self._percent_to_volume(volume_percent)
        self._current_alias: Optional[str] = None
        self._current_process: Optional[subprocess.Popen] = None
        self._lock = threading.Lock()
        self._queue: "queue.Queue[tuple[Optional[str], str]]" = queue.Queue()
        self._thread = threading.Thread(target=self._run, daemon=True)

        self._rvc_enabled = rvc_enabled
        self._rvc_base_url = rvc_base_url
        self._rvc_scope = rvc_scope
        self._rvc_pitch = rvc_pitch
        self._rvc_index_rate = rvc_index_rate
        self._rvc_protect = rvc_protect
        self._rvc_f0method = rvc_f0method

    @staticmethod
    def _percent_to_volume(percent: int) -> str:
        percent = max(0, min(200, percent))
        return f"{percent - 100:+d}%"

    def start(self) -> None:
        self._thread.start()

    def say(self, author: str, text: str) -> None:
        self._queue.put((author, text))

    def say_event(self, text: str) -> None:
        self._queue.put((None, text))

    def set_volume_percent(self, percent: int) -> None:
        self._volume = self._percent_to_volume(percent)
        log.info("Громкость TTS: %d%%", max(0, min(200, percent)))

    def set_voice_ru(self, voice: str) -> None:
        self._voice_ru = voice

    def set_voice_en(self, voice: str) -> None:
        self._voice_en = voice

    def set_rate(self, rate: str) -> None:
        self._rate = rate

    def set_say_author(self, value: bool) -> None:
        self._say_author = value

    def set_rvc_enabled(self, value: bool) -> None:
        self._rvc_enabled = value

    def set_rvc_base_url(self, base_url: str) -> None:
        self._rvc_base_url = base_url

    def set_rvc_scope(self, scope: str) -> None:
        self._rvc_scope = scope

    def set_rvc_params(
        self,
        pitch: Optional[int] = None,
        index_rate: Optional[float] = None,
        protect: Optional[float] = None,
        f0method: Optional[str] = None,
    ) -> None:
        if pitch is not None:
            self._rvc_pitch = pitch
        if index_rate is not None:
            self._rvc_index_rate = index_rate
        if protect is not None:
            self._rvc_protect = protect
        if f0method is not None:
            self._rvc_f0method = f0method

    def skip_current(self) -> bool:
        if IS_WINDOWS:
            with self._lock:
                alias = self._current_alias
            if not alias:
                return False
            try:
                _mci(f"stop {alias}")
            except Exception:
                log.exception("Не удалось прервать воспроизведение")
            return True

        with self._lock:
            proc = self._current_process
        if not proc:
            return False
        proc.terminate()
        return True

    def clear_queue(self) -> int:
        removed = 0
        try:
            while True:
                self._queue.get_nowait()
                removed += 1
        except queue.Empty:
            pass
        return removed

    def _run(self) -> None:
        while True:
            author, text = self._queue.get()
            try:
                self._speak(author, text)
            except Exception:
                log.exception("Ошибка озвучки")

    def _pick_voice(self, text: str) -> str:
        if CYRILLIC_RE.search(text):
            return self._voice_ru
        if LATIN_RE.search(text):
            return self._voice_en
        return self._voice_default

    def _speak(self, author: Optional[str], text: str) -> None:
        clean_text = URL_RE.sub("ссылка", text).strip()
        if not clean_text:
            return
        clean_text = clean_text[: self._max_chars]

        phrase = f"{author}: {clean_text}" if (self._say_author and author) else clean_text
        voice = self._pick_voice(clean_text)
        is_event = author is None

        with tempfile.TemporaryDirectory() as tmp_dir:
            mp3_path = Path(tmp_dir) / "speech.mp3"
            wav_path = Path(tmp_dir) / "speech_rvc.wav"
            final_path = asyncio.run(self._synthesize_and_convert(phrase, voice, mp3_path, wav_path, is_event))
            self._play(str(final_path))

    def _rvc_applies(self, is_event: bool) -> bool:
        if not self._rvc_enabled:
            return False
        if self._rvc_scope == "alerts":
            return is_event
        return True

    async def _synthesize_and_convert(
        self, phrase: str, voice: str, mp3_path: Path, wav_path: Path, is_event: bool
    ) -> Path:
        communicate = edge_tts.Communicate(phrase, voice, rate=self._rate, volume=self._volume)
        await communicate.save(str(mp3_path))

        if not self._rvc_applies(is_event):
            return mp3_path

        try:
            async with aiohttp.ClientSession() as session:
                converted = await rvc_client.convert(
                    session,
                    self._rvc_base_url,
                    mp3_path.read_bytes(),
                    self._rvc_pitch,
                    self._rvc_index_rate,
                    self._rvc_protect,
                    self._rvc_f0method,
                )
            wav_path.write_bytes(converted)
            return wav_path
        except Exception:
            log.exception("RVC-конвертация не удалась, играю обычный голос")
            return mp3_path

    def _play(self, path: str) -> None:
        if IS_WINDOWS:
            self._play_windows(path)
        else:
            self._play_subprocess(path)

    def _play_windows(self, path: str) -> None:
        alias = f"tts_{threading.get_ident()}"
        mci_type = "waveaudio" if path.lower().endswith(".wav") else "mpegvideo"
        with self._lock:
            self._current_alias = alias
        try:
            _mci(f'open "{path}" type {mci_type} alias {alias}')
            try:
                _mci(f"play {alias} wait")
            finally:
                _mci(f"close {alias}")
        finally:
            with self._lock:
                self._current_alias = None

    def _play_subprocess(self, path: str) -> None:
        cmd = PLAYER_CMD.get(sys.platform)
        if not cmd:
            log.error("Воспроизведение TTS не поддерживается на этой ОС (%s)", sys.platform)
            return
        proc = subprocess.Popen([*cmd, path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with self._lock:
            self._current_process = proc
        try:
            proc.wait()
        finally:
            with self._lock:
                self._current_process = None
