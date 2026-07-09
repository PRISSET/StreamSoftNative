import json
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Optional

COMMANDS_FILE = Path(__file__).parent / "chat_commands.json"
DEFAULT_COOLDOWN = 15


@dataclass
class ChatCommand:
    trigger: str
    response: str
    enabled: bool = True
    cooldown: int = DEFAULT_COOLDOWN


class CommandsStore:
    def __init__(self) -> None:
        self._commands: "list[ChatCommand]" = []
        self._last_used: "dict[str, float]" = {}
        self.load()

    def load(self) -> None:
        if COMMANDS_FILE.exists():
            try:
                data = json.loads(COMMANDS_FILE.read_text(encoding="utf-8"))
                self._commands = [ChatCommand(**c) for c in data]
            except Exception:
                self._commands = []

    def save(self) -> None:
        COMMANDS_FILE.write_text(
            json.dumps([asdict(c) for c in self._commands], ensure_ascii=False, indent=2), encoding="utf-8"
        )

    def list(self) -> "list[dict]":
        return [asdict(c) for c in self._commands]

    @staticmethod
    def _normalize(trigger: str) -> str:
        trigger = trigger.strip().lower()
        if not trigger:
            return trigger
        if not trigger.startswith("!"):
            trigger = f"!{trigger}"
        return trigger

    def add(self, trigger: str, response: str, cooldown: int = DEFAULT_COOLDOWN) -> None:
        trigger = self._normalize(trigger)
        if not trigger or not response:
            return
        self._commands = [c for c in self._commands if c.trigger != trigger]
        self._commands.append(ChatCommand(trigger=trigger, response=response, cooldown=max(0, cooldown)))
        self.save()

    def remove(self, trigger: str) -> None:
        trigger = self._normalize(trigger)
        self._commands = [c for c in self._commands if c.trigger != trigger]
        self._last_used.pop(trigger, None)
        self.save()

    def set_enabled(self, trigger: str, enabled: bool) -> None:
        trigger = self._normalize(trigger)
        for c in self._commands:
            if c.trigger == trigger:
                c.enabled = enabled
        self.save()

    def match(self, text: str) -> Optional[str]:
        stripped = text.strip()
        if not stripped:
            return None
        first_word = stripped.split()[0].lower()

        for c in self._commands:
            if not c.enabled or c.trigger != first_word:
                continue
            now = time.time()
            last = self._last_used.get(c.trigger, 0.0)
            if now - last < c.cooldown:
                return None
            self._last_used[c.trigger] = now
            return c.response
        return None
