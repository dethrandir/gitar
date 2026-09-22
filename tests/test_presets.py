"""Tests for rig presets."""

from __future__ import annotations

from pathlib import Path

import pytest

from gitar_server.config import config_dir
from gitar_server.presets import (
    Preset,
    PresetError,
    PresetNotFoundError,
    delete_preset,
    list_presets,
    load_preset,
    presets_dir,
    safe_name,
    save_preset,
)


@pytest.fixture()
def preset_home(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    monkeypatch.setenv("GITAR_PRESETS_DIR", str(tmp_path))
    return tmp_path


def _preset(**overrides: object) -> Preset:
    payload: dict[str, object] = {
        "name": "clean",
        "model_path": "/models/amp.nam",
        "cab_ir_path": "/cabs/marshal.wav",
        "gain": 0.7,
        "gate_enabled": False,
        "gate_threshold_db": -40.0,
        "eq_low_db": 2.0,
        "eq_mid_db": -1.5,
        "eq_high_db": 3.0,
    }
    payload.update(overrides)
    return Preset.model_validate(payload)


def test_presets_dir_honors_env_override(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_PRESETS_DIR", str(tmp_path))

    assert presets_dir() == tmp_path


def test_presets_dir_defaults_under_config_dir(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.delenv("GITAR_PRESETS_DIR", raising=False)
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))

    assert presets_dir() == config_dir() / "presets"


def test_save_and_load_round_trip(preset_home: Path) -> None:
    original = _preset()

    written = save_preset(original)

    assert written == preset_home / "clean.json"
    assert written.exists()
    assert load_preset("clean") == original


def test_save_preset_overwrites_atomically(preset_home: Path) -> None:
    save_preset(_preset(gain=1.0))
    save_preset(_preset(gain=2.0))

    assert load_preset("clean").gain == 2.0
    assert [entry.name for entry in preset_home.iterdir()] == ["clean.json"]


def test_list_presets_sorted_ignores_non_json(preset_home: Path) -> None:
    save_preset(_preset(name="beta"))
    save_preset(_preset(name="alpha"))
    (preset_home / "notes.txt").write_text("ignore me", encoding="utf-8")

    assert list_presets() == ["alpha", "beta"]


def test_list_presets_missing_directory_returns_empty(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("GITAR_PRESETS_DIR", str(tmp_path / "nope"))

    assert list_presets() == []


def test_delete_preset_removes_file(preset_home: Path) -> None:
    save_preset(_preset())

    assert delete_preset("clean") is True
    assert delete_preset("clean") is False
    assert list_presets() == []


def test_load_missing_preset_raises_not_found(preset_home: Path) -> None:
    with pytest.raises(PresetNotFoundError):
        load_preset("nope")

    with pytest.raises(PresetError):
        load_preset("nope")


def test_load_invalid_json_raises_preset_error(preset_home: Path) -> None:
    (preset_home / "broken.json").write_text("{not json", encoding="utf-8")

    with pytest.raises(PresetError):
        load_preset("broken")


def test_load_invalid_schema_raises_preset_error(preset_home: Path) -> None:
    (preset_home / "bad.json").write_text('{"gain": "loud"}', encoding="utf-8")

    with pytest.raises(PresetError):
        load_preset("bad")


def test_save_preset_rejects_unsafe_name(preset_home: Path) -> None:
    with pytest.raises(ValueError):
        save_preset(_preset(name="../escape"))


@pytest.mark.parametrize(
    "bad_name",
    ["../etc/passwd", "a/b", "a\\b", "..", "", "   ", "a\x00b", "line\nbreak"],
)
def test_safe_name_rejects_unsafe_names(bad_name: str) -> None:
    with pytest.raises(ValueError):
        safe_name(bad_name)


def test_safe_name_trims_whitespace() -> None:
    assert safe_name("  clean  ") == "clean"
