from dataclasses import dataclass, field


@dataclass
class ChatMessage:
    platform: str
    author: str
    text: str


@dataclass
class StreamEvent:
    kind: str
    user: str
    detail: str = ""
    extra: dict = field(default_factory=dict)
