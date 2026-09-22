"""Windows WASAPI backend placeholder.

The native ``gitar-engine`` binary that will own the WASAPI audio path is not
implemented yet, so this backend only reports availability and refuses to
mutate anything.
"""

from __future__ import annotations

import shutil
import sys

from gitar_server.config import Config

from .base import AudioBackend, BackendError, BackendStatus, LevelReading, Route

ENGINE_BINARY = "gitar-engine"


def _engine_found() -> bool:
    return shutil.which(ENGINE_BINARY) is not None


class WasapiBackend(AudioBackend):
    name = "wasapi"

    def is_available(self) -> bool:
        return sys.platform.startswith("win") and _engine_found()

    def connect_direct(self, config: Config) -> None:
        raise BackendError(self._unavailable("direct routing"))

    def connect_amp(self, config: Config) -> None:
        raise BackendError(self._unavailable("amp routing"))

    def disconnect(self, config: Config) -> None:
        raise BackendError(self._unavailable("disconnect"))

    def set_volume(self, config: Config, percent: int) -> None:
        raise BackendError(self._unavailable("volume control"))

    def status(self, config: Config) -> BackendStatus:
        engine = ENGINE_BINARY if _engine_found() else "missing"
        details = {
            "input": config.input,
            "channel": config.channel,
            "output": config.output,
            "latency": config.latency,
            "engine": engine,
        }
        return BackendStatus(
            route=Route.OFF,
            connected=False,
            engine_running=False,
            details=details,
        )

    def measure_level(self, config: Config, seconds: float = 10.0) -> LevelReading:
        raise BackendError(self._unavailable("level measurement"))

    def list_tones(self) -> list[str]:
        return []

    def load_tone(self, config: Config, tone: str) -> None:
        raise BackendError(self._unavailable("tone loading"))

    @staticmethod
    def _unavailable(action: str) -> str:
        return (
            f"Windows WASAPI {action} is not available yet: the native "
            f"{ENGINE_BINARY} binary is missing or the engine is not implemented"
        )
