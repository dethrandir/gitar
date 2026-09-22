"""Backend abstraction shared by every audio backend implementation."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum

from gitar_server.config import Config


class Route(str, Enum):
    OFF = "off"
    DIRECT = "direct"
    AMP = "amp"


class BackendError(RuntimeError):
    """Raised when a backend operation fails."""


@dataclass(frozen=True)
class BackendStatus:
    route: Route
    connected: bool = False
    engine_running: bool = False
    details: dict[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class LevelReading:
    peak_dbfs: float
    advice: str


class AudioBackend(ABC):
    name: str = "base"

    @abstractmethod
    def is_available(self) -> bool: ...

    @abstractmethod
    def connect_direct(self, config: Config) -> None: ...

    @abstractmethod
    def connect_amp(self, config: Config) -> None: ...

    @abstractmethod
    def disconnect(self, config: Config) -> None: ...

    @abstractmethod
    def set_volume(self, config: Config, percent: int) -> None: ...

    @abstractmethod
    def status(self, config: Config) -> BackendStatus: ...

    @abstractmethod
    def measure_level(self, config: Config, seconds: float = 10.0) -> LevelReading: ...

    @abstractmethod
    def list_tones(self) -> list[str]: ...

    @abstractmethod
    def load_tone(self, config: Config, tone: str) -> None: ...
