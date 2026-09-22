"""Audio backend abstraction and platform factory."""

from .base import AudioBackend, BackendError, BackendStatus, LevelReading, Route
from .factory import get_backend
from .null import NullBackend

__all__ = [
    "AudioBackend",
    "BackendError",
    "BackendStatus",
    "LevelReading",
    "NullBackend",
    "Route",
    "get_backend",
]
