"""In-memory backend used for tests and as a platform fallback."""

from __future__ import annotations

from gitar_server.config import Config

from .base import AudioBackend, BackendError, BackendStatus, LevelReading, Route

MIN_VOLUME = 0
MAX_VOLUME = 150
_KNOWN_TONES: frozenset[str] = frozenset({"clean", "crunch", "army"})


class NullBackend(AudioBackend):
    name = "null"

    def __init__(self) -> None:
        self._route = Route.OFF
        self._connected = False
        self._engine_running = False
        self._volume = 100
        self._tone: str | None = None

    def is_available(self) -> bool:
        return True

    def connect_direct(self, config: Config) -> None:
        self._route = Route.DIRECT
        self._connected = True
        self._engine_running = False

    def connect_amp(self, config: Config) -> None:
        self._route = Route.AMP
        self._connected = True
        self._engine_running = True

    def disconnect(self, config: Config) -> None:
        self._route = Route.OFF
        self._connected = False
        self._engine_running = False

    def set_volume(self, config: Config, percent: int) -> None:
        if isinstance(percent, bool) or not isinstance(percent, int):
            raise BackendError(f"volume must be an int, got {type(percent).__name__}")
        if percent < MIN_VOLUME or percent > MAX_VOLUME:
            raise BackendError(f"volume must be {MIN_VOLUME}..{MAX_VOLUME}, got {percent}")
        self._volume = percent

    def status(self, config: Config) -> BackendStatus:
        details = {"volume": str(self._volume)}
        if self._tone is not None:
            details["tone"] = self._tone
        return BackendStatus(
            route=self._route,
            connected=self._connected,
            engine_running=self._engine_running,
            details=details,
        )

    def measure_level(self, config: Config, seconds: float = 10.0) -> LevelReading:
        return LevelReading(peak_dbfs=-120.0, advice="No signal backend available.")

    def list_tones(self) -> list[str]:
        return sorted(_KNOWN_TONES)

    def load_tone(self, config: Config, tone: str) -> None:
        if tone not in _KNOWN_TONES:
            raise BackendError(f"unknown tone {tone!r}")
        self._tone = tone
