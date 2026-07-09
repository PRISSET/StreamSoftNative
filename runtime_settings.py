import json
from dataclasses import asdict, dataclass
from pathlib import Path

SETTINGS_FILE = Path(__file__).parent / "runtime_settings.json"


@dataclass
class RuntimeSettings:
    theme: str = "minimal"
    tts_voice_ru: str = "ru-RU-DmitryNeural"
    tts_voice_en: str = "en-US-GuyNeural"
    tts_rate: str = "+0%"
    tts_volume: int = 100
    tts_say_author: bool = True
    event_volume: int = 100
    overlay_scale: float = 1.0

    rvc_enabled: bool = False
    rvc_base_url: str = "http://127.0.0.1:8100"
    rvc_model: str = "ayaka"
    rvc_scope: str = "alerts"
    rvc_pitch: int = 12
    rvc_index_rate: float = 0.3
    rvc_protect: float = 0.5
    rvc_f0method: str = "rmvpe"

    @classmethod
    def load(cls, defaults: "RuntimeSettings") -> "RuntimeSettings":
        if SETTINGS_FILE.exists():
            try:
                data = json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
                merged = {**asdict(defaults), **data}
                return cls(**merged)
            except Exception:
                pass
        return defaults

    def save(self) -> None:
        SETTINGS_FILE.write_text(json.dumps(asdict(self), ensure_ascii=False, indent=2), encoding="utf-8")

    def as_dict(self) -> dict:
        return asdict(self)
