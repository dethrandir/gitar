"""Tests for the ``gitard`` command-line interface."""

from __future__ import annotations

from pathlib import Path
from typing import Any

import pytest

from gitar_server import __version__, cli


def _record_uvicorn_run(monkeypatch: pytest.MonkeyPatch) -> list[tuple[Any, dict[str, Any]]]:
    calls: list[tuple[Any, dict[str, Any]]] = []

    def fake_run(app: Any, **kwargs: Any) -> None:
        calls.append((app, kwargs))

    monkeypatch.setattr(cli.uvicorn, "run", fake_run)
    return calls


def test_serve_uses_default_host_and_port(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _record_uvicorn_run(monkeypatch)
    assert cli.main(["serve"]) == 0
    assert len(calls) == 1
    app, kwargs = calls[0]
    assert app is not None
    assert kwargs == {"host": "127.0.0.1", "port": 7343}


def test_serve_passes_host_and_port_through(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _record_uvicorn_run(monkeypatch)
    assert cli.main(["serve", "--host", "0.0.0.0", "--port", "9000"]) == 0
    _, kwargs = calls[0]
    assert kwargs == {"host": "0.0.0.0", "port": 9000}


def test_serve_reload_uses_import_string_factory(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _record_uvicorn_run(monkeypatch)
    assert cli.main(["serve", "--reload"]) == 0
    app, kwargs = calls[0]
    assert app == "gitar_server.api:create_app"
    assert kwargs == {
        "factory": True,
        "host": "127.0.0.1",
        "port": 7343,
        "reload": True,
    }


def test_version_command(capsys: pytest.CaptureFixture[str]) -> None:
    assert cli.main(["version"]) == 0
    assert capsys.readouterr().out.strip() == f"gitar {__version__}"


def test_version_flag(capsys: pytest.CaptureFixture[str]) -> None:
    assert cli.main(["--version"]) == 0
    assert capsys.readouterr().out.strip() == f"gitar {__version__}"


def test_config_command_prints_path_and_config(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    assert cli.main(["config"]) == 0
    out = capsys.readouterr().out
    assert str(tmp_path / "config.json") in out
    assert '"latency"' in out


def test_unknown_command_returns_two_and_writes_stderr(
    capsys: pytest.CaptureFixture[str],
) -> None:
    assert cli.main(["bogus"]) == 2
    assert capsys.readouterr().err


def test_no_arguments_defaults_to_serve(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _record_uvicorn_run(monkeypatch)
    assert cli.main([]) == 0
    _, kwargs = calls[0]
    assert kwargs == {"host": "127.0.0.1", "port": 7343}


def test_open_schedules_browser_for_loopback(monkeypatch: pytest.MonkeyPatch) -> None:
    _record_uvicorn_run(monkeypatch)
    opened: list[str] = []

    def fake_open(url: str, *args: Any, **kwargs: Any) -> bool:
        opened.append(url)
        return True

    monkeypatch.setattr(cli.webbrowser, "open", fake_open)
    monkeypatch.setattr(cli, "_schedule_open", lambda delay, callback: callback())
    assert cli.main(["serve", "--open"]) == 0
    assert opened == ["http://127.0.0.1:7343"]
