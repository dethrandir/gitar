"""Select the audio backend for the running platform."""

from __future__ import annotations

import importlib
import sys

from .base import AudioBackend
from .null import NullBackend

_PLATFORM_BACKENDS: dict[str, tuple[str, str]] = {
    "linux": (".pipewire", "PipeWireBackend"),
    "win32": (".wasapi", "WasapiBackend"),
    "cygwin": (".wasapi", "WasapiBackend"),
}


def get_backend(platform: str | None = None) -> AudioBackend:
    """Return the backend for the current platform.

    linux -> PipeWireBackend, win32/cygwin -> WasapiBackend, otherwise NullBackend.
    Falls back to NullBackend if the platform module cannot be imported.
    """
    key = platform if platform is not None else sys.platform
    mapping = _PLATFORM_BACKENDS.get(key)
    if mapping is None:
        return NullBackend()
    module_name, class_name = mapping
    try:
        module = importlib.import_module(module_name, __package__)
    except ImportError:
        return NullBackend()
    backend_class = getattr(module, class_name, None)
    if backend_class is None:
        return NullBackend()
    backend: AudioBackend = backend_class()
    return backend
