"""Tests for the Windows WASAPI backend placeholder."""

from __future__ import annotations

import shutil
import sys
from collections.abc import Callable, Iterator

import pytest

from gitar_server.backends import BackendError, NullBackend, Route, get_backend
from gitar_server.backends.wasapi import WasapiBackend
from gitar_server.config import Config

ENGINE_PATH = "/usr/bin/gitar-engine"


@pytest.fixture
def windows_with_engine(monkeypatch: pytest.MonkeyPatch) -> Iterator[None]:
    monkeypatch.setattr(sys, "platform", "win32")
    monkeypatch.setattr(shutil, "which", lambda name: ENGINE_PATH)
    yield


@pytest.fixture
def windows_without_engine(monkeypatch: pytest.MonkeyPatch) -> Iterator[None]:
    monkeypatch.setattr(sys, "platform", "win32")
    monkeypatch.setattr(shutil, "which", lambda name: None)
    yield


def test_name() -> None:
    assert WasapiBackend().name == "wasapi"


def test_is_available_windows_with_engine(windows_with_engine: None) -> None:
    assert WasapiBackend().is_available() is True


def test_is_available_windows_without_engine(windows_without_engine: None) -> None:
    assert WasapiBackend().is_available() is False


def test_is_available_non_windows(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(sys, "platform", "linux")
    monkeypatch.setattr(shutil, "which", lambda name: ENGINE_PATH)
    assert WasapiBackend().is_available() is False


def test_status_is_off_and_reports_engine_found(windows_with_engine: None) -> None:
    status = WasapiBackend().status(Config(input="guitar", channel="FL", output="hp"))
    assert status.route is Route.OFF
    assert status.connected is False
    assert status.engine_running is False
    assert status.details["input"] == "guitar"
    assert status.details["channel"] == "FL"
    assert status.details["output"] == "hp"
    assert status.details["latency"] == Config().latency
    assert status.details["engine"] == "gitar-engine"


def test_status_reports_engine_missing(windows_without_engine: None) -> None:
    assert WasapiBackend().status(Config()).details["engine"] == "missing"


def test_status_never_raises_without_engine(windows_without_engine: None) -> None:
    status = WasapiBackend().status(Config())
    assert status.route is Route.OFF


def test_list_tones_is_empty() -> None:
    assert WasapiBackend().list_tones() == []


@pytest.mark.parametrize(
    ("action", "call"),
    [
        ("direct routing", lambda backend: backend.connect_direct(Config())),
        ("amp routing", lambda backend: backend.connect_amp(Config())),
        ("disconnect", lambda backend: backend.disconnect(Config())),
        ("volume control", lambda backend: backend.set_volume(Config(), 50)),
        ("level measurement", lambda backend: backend.measure_level(Config())),
        ("tone loading", lambda backend: backend.load_tone(Config(), "clean")),
    ],
)
def test_mutating_methods_raise(action: str, call: Callable[[WasapiBackend], None]) -> None:
    backend = WasapiBackend()
    with pytest.raises(BackendError) as excinfo:
        call(backend)
    message = str(excinfo.value)
    assert action in message
    assert "gitar-engine" in message


def test_get_backend_win32_returns_wasapi() -> None:
    assert isinstance(get_backend("win32"), WasapiBackend)


def test_get_backend_darwin_returns_null() -> None:
    assert isinstance(get_backend("darwin"), NullBackend)
