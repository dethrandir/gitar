"""Tests for the gitar_server configuration module."""

import json
import os
import sys
from pathlib import Path

import pytest

from gitar_server.config import (
    DEFAULT_LATENCY,
    Config,
    ConfigError,
    _resolve_config_dir,
    config_dir,
    config_path,
    legacy_config_path,
    load_config,
    migrate_legacy_if_needed,
    parse_legacy,
    save_config,
)


def test_defaults() -> None:
    config = Config()
    assert config.input == ""
    assert config.channel == ""
    assert config.output == ""
    assert config.latency == DEFAULT_LATENCY == "128/48000"
    assert config.backend == ""


def test_config_dir_env_override(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    target = tmp_path / "custom"
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(target))
    assert config_dir() == target
    assert config_path() == target / "config.json"
    assert legacy_config_path() == target / "config"


def test_config_dir_xdg_on_linux(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("GITAR_CONFIG_DIR", raising=False)
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    monkeypatch.setattr(sys, "platform", "linux")
    monkeypatch.setattr(os, "name", "posix")
    assert config_dir() == tmp_path / "xdg" / "gitar"


def test_config_dir_home_fallback(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("GITAR_CONFIG_DIR", raising=False)
    monkeypatch.delenv("XDG_CONFIG_HOME", raising=False)
    monkeypatch.setattr(sys, "platform", "linux")
    monkeypatch.setattr(os, "name", "posix")
    monkeypatch.setattr(Path, "home", lambda: tmp_path)
    assert config_dir() == tmp_path / ".config" / "gitar"


def test_config_dir_windows_appdata(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("GITAR_CONFIG_DIR", raising=False)
    monkeypatch.setenv("APPDATA", str(tmp_path / "appdata"))
    monkeypatch.setattr(sys, "platform", "win32")
    assert config_dir() == tmp_path / "appdata" / "gitar"


def test_resolve_config_dir_windows_os_name(tmp_path: Path) -> None:
    resolved = _resolve_config_dir({"APPDATA": str(tmp_path / "roaming")}, "posix", tmp_path, "nt")
    assert resolved == tmp_path / "roaming" / "gitar"


def test_resolve_config_dir_windows_without_appdata(tmp_path: Path) -> None:
    resolved = _resolve_config_dir({}, "win32", tmp_path, "nt")
    assert resolved == tmp_path / "AppData" / "Roaming" / "gitar"


def test_config_dir_macos(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("GITAR_CONFIG_DIR", raising=False)
    monkeypatch.setattr(sys, "platform", "darwin")
    monkeypatch.setattr(os, "name", "posix")
    monkeypatch.setattr(Path, "home", lambda: tmp_path)
    assert config_dir() == tmp_path / "Library" / "Application Support" / "gitar"


@pytest.mark.parametrize(
    "bad_latency",
    ["", "128", "128/", "/48000", "abc", "128/48000/1", "-1/2", "1.5/2"],
)
def test_invalid_latency_rejected(bad_latency: str) -> None:
    with pytest.raises(ValueError):
        Config(latency=bad_latency)


def test_valid_latency_accepted() -> None:
    assert Config(latency="256/44100").latency == "256/44100"


def test_load_config_missing_returns_defaults(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    assert load_config() == Config()


def test_save_and_load_round_trip(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    original = Config(
        input="alsa_input.usb-123-input",
        channel="capture_FR",
        output="alsa_output.usb-456-analog-stereo",
        latency="256/48000",
        backend="pipewire",
    )
    written = save_config(original)
    assert written == config_path()
    assert written.exists()
    assert load_config() == original


def test_save_config_writes_pretty_json(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    save_config(Config())
    text = config_path().read_text(encoding="utf-8")
    assert text.endswith("\n")
    assert "\n  " in text
    assert json.loads(text) == Config().model_dump()


def test_save_config_overwrites_atomically(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    save_config(Config(input="first"))
    save_config(Config(input="second"))
    assert load_config().input == "second"
    assert [entry.name for entry in tmp_path.iterdir()] == ["config.json"]


def test_load_config_corrupt_json_raises(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    config_path().write_text("{not valid json", encoding="utf-8")
    with pytest.raises(ConfigError):
        load_config()


def test_load_config_invalid_schema_raises(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    config_path().write_text('{"latency": "not-a-latency"}', encoding="utf-8")
    with pytest.raises(ConfigError):
        load_config()


def test_parse_legacy_handles_quotes_escapes_comments_and_export() -> None:
    text = (
        "# a comment\n"
        "\n"
        "GIRIS='alsa_input.usb-123-input'\n"
        "export KANAL=capture_FR\n"
        "CIKIS=alsa_output.usb-456-analog-stereo\n"
        "GECIKME='128/48000'\n"
        "IGNORED LINE\n"
        "UNKNOWN=value\n"
        "ESCAPED='it\\'s\\ a\\ test'\n"
        "PLAIN=a\\ b\n"
    )
    parsed = parse_legacy(text)
    assert parsed["GIRIS"] == "alsa_input.usb-123-input"
    assert parsed["KANAL"] == "capture_FR"
    assert parsed["CIKIS"] == "alsa_output.usb-456-analog-stereo"
    assert parsed["GECIKME"] == "128/48000"
    assert parsed["UNKNOWN"] == "value"
    assert parsed["ESCAPED"] == "it's a test"
    assert parsed["PLAIN"] == "a b"
    assert "IGNORED" not in parsed


def test_parse_legacy_empty_text() -> None:
    assert parse_legacy("") == {}


def test_migrate_legacy_creates_config_and_returns_it(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    legacy_config_path().write_text(
        "GIRIS='alsa_input.usb-999-input'\n"
        "KANAL=capture_FL\n"
        "CIKIS='alsa_output.usb-999-analog-stereo'\n"
        "GECIKME='256/48000'\n",
        encoding="utf-8",
    )
    migrated = migrate_legacy_if_needed()
    assert migrated is not None
    assert migrated.input == "alsa_input.usb-999-input"
    assert migrated.channel == "capture_FL"
    assert migrated.output == "alsa_output.usb-999-analog-stereo"
    assert migrated.latency == "256/48000"
    assert config_path().exists()
    assert load_config() == migrated


def test_migrate_legacy_uses_default_latency_when_missing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    legacy_config_path().write_text("GIRIS='in'\n", encoding="utf-8")
    migrated = migrate_legacy_if_needed()
    assert migrated is not None
    assert migrated.latency == DEFAULT_LATENCY


def test_migrate_legacy_noop_when_config_exists(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    save_config(Config(input="existing"))
    legacy_config_path().write_text("GIRIS='legacy'\n", encoding="utf-8")
    assert migrate_legacy_if_needed() is None
    assert load_config().input == "existing"


def test_migrate_legacy_returns_none_without_legacy(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))
    assert migrate_legacy_if_needed() is None
    assert not config_path().exists()
