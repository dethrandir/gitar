"""Tests for the audio backend abstraction, null backend, and factory."""

from typing import Any

import pytest

from gitar_server.backends import (
    AudioBackend,
    BackendError,
    BackendStatus,
    LevelReading,
    NullBackend,
    Route,
    get_backend,
)
from gitar_server.config import Config

TONES = ["army", "clean", "crunch"]


def test_exports_are_available() -> None:
    assert AudioBackend is not None
    assert BackendError is not None
    assert BackendStatus is not None
    assert LevelReading is not None
    assert NullBackend is not None
    assert Route is not None
    assert get_backend is not None


def test_route_members() -> None:
    assert Route.OFF.value == "off"
    assert Route.DIRECT.value == "direct"
    assert Route.AMP.value == "amp"


def test_null_backend_is_audio_backend_and_available() -> None:
    backend = NullBackend()
    assert isinstance(backend, AudioBackend)
    assert backend.name == "null"
    assert backend.is_available() is True


def test_null_backend_initial_status() -> None:
    status = NullBackend().status(Config())
    assert status.route is Route.OFF
    assert status.connected is False
    assert status.engine_running is False


def test_null_backend_connect_direct() -> None:
    backend = NullBackend()
    backend.connect_direct(Config())
    status = backend.status(Config())
    assert status.route is Route.DIRECT
    assert status.connected is True
    assert status.engine_running is False


def test_null_backend_connect_amp() -> None:
    backend = NullBackend()
    backend.connect_amp(Config())
    status = backend.status(Config())
    assert status.route is Route.AMP
    assert status.connected is True
    assert status.engine_running is True


def test_null_backend_disconnect_resets() -> None:
    backend = NullBackend()
    backend.connect_amp(Config())
    backend.disconnect(Config())
    status = backend.status(Config())
    assert status.route is Route.OFF
    assert status.connected is False
    assert status.engine_running is False


def test_null_backend_set_volume_accepts_range() -> None:
    backend = NullBackend()
    backend.set_volume(Config(), 0)
    backend.set_volume(Config(), 100)
    backend.set_volume(Config(), 150)
    assert backend.status(Config()).details["volume"] == "150"


@pytest.mark.parametrize("percent", [-1, 151, 1000])
def test_null_backend_set_volume_rejects_out_of_range(percent: int) -> None:
    backend = NullBackend()
    with pytest.raises(BackendError):
        backend.set_volume(Config(), percent)


def test_null_backend_set_volume_rejects_non_int() -> None:
    backend = NullBackend()
    bad: Any = "loud"
    with pytest.raises(BackendError):
        backend.set_volume(Config(), bad)
    also_bad: Any = 12.5
    with pytest.raises(BackendError):
        backend.set_volume(Config(), also_bad)


def test_null_backend_measure_level() -> None:
    reading = NullBackend().measure_level(Config())
    assert reading == LevelReading(peak_dbfs=-120.0, advice="No signal backend available.")


def test_null_backend_list_tones_sorted() -> None:
    assert NullBackend().list_tones() == TONES


def test_null_backend_load_known_tone() -> None:
    backend = NullBackend()
    backend.load_tone(Config(), "clean")
    assert backend.status(Config()).details["tone"] == "clean"


def test_null_backend_load_unknown_tone() -> None:
    backend = NullBackend()
    with pytest.raises(BackendError):
        backend.load_tone(Config(), "nope")


def test_get_backend_linux_returns_pipewire() -> None:
    from gitar_server.backends.pipewire import PipeWireBackend

    assert isinstance(get_backend("linux"), PipeWireBackend)


def test_get_backend_windows_falls_back_to_null() -> None:
    assert isinstance(get_backend("win32"), NullBackend)
    assert isinstance(get_backend("cygwin"), NullBackend)


def test_get_backend_unknown_platform_is_null() -> None:
    assert isinstance(get_backend("foo"), NullBackend)


def test_get_backend_default_never_raises() -> None:
    assert isinstance(get_backend(), AudioBackend)
