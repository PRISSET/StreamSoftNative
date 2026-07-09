class ModerationState:
    def __init__(self) -> None:
        self._muted: set[str] = set()

    def mute(self, username: str) -> None:
        self._muted.add(self._normalize(username))

    def unmute(self, username: str) -> None:
        self._muted.discard(self._normalize(username))

    def is_muted(self, username: str) -> bool:
        return self._normalize(username) in self._muted

    def list_muted(self) -> list[str]:
        return sorted(self._muted)

    @staticmethod
    def _normalize(username: str) -> str:
        return username.strip().lstrip("@").lower()
