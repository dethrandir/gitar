"""Tests for the neural model (.nam) registry."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from gitar_server.config import config_dir
from gitar_server.models import ModelInfo, models_dir, parse_nam_metadata, scan_models


def _nam(**overrides: object) -> str:
    payload: dict[str, object] = {
        "version": "0.7.0",
        "architecture": "WaveNet",
        "config": {},
        "weights": [1.0],
        "sample_rate": 48000,
        "metadata": {"name": "Clean Deluxe"},
    }
    payload.update(overrides)
    return json.dumps(payload)


def test_parse_reads_architecture_sample_rate_and_name() -> None:
    metadata = parse_nam_metadata(_nam())

    assert metadata["architecture"] == "WaveNet"
    assert metadata["sample_rate"] == 48000.0
    assert metadata["name"] == "Clean Deluxe"


def test_parse_without_metadata_name_returns_empty_string() -> None:
    metadata = parse_nam_metadata(_nam(metadata={}))

    assert metadata["name"] == ""
    assert metadata["architecture"] == "WaveNet"


def test_parse_missing_sample_rate_is_negative_one() -> None:
    metadata = parse_nam_metadata(json.dumps({"architecture": "Linear"}))

    assert metadata["sample_rate"] == -1.0
    assert metadata["architecture"] == "Linear"
    assert metadata["name"] == ""


def test_parse_invalid_json_raises_value_error() -> None:
    with pytest.raises(ValueError):
        parse_nam_metadata("{not json")


def test_parse_non_object_raises_value_error() -> None:
    with pytest.raises(ValueError):
        parse_nam_metadata("[1, 2, 3]")


def test_scan_models_sorted_skips_malformed_and_ignores_other_files(tmp_path: Path) -> None:
    (tmp_path / "b.nam").write_text(_nam(metadata={"name": "B"}), encoding="utf-8")
    (tmp_path / "a.nam").write_text(_nam(), encoding="utf-8")
    (tmp_path / "broken.nam").write_text("{not json", encoding="utf-8")
    (tmp_path / "notes.txt").write_text("ignore me", encoding="utf-8")

    found = scan_models(tmp_path)

    assert [model.name for model in found] == ["a", "b"]
    assert all(isinstance(model, ModelInfo) for model in found)
    first = found[0]
    assert first.path == tmp_path / "a.nam"
    assert first.architecture == "WaveNet"
    assert first.sample_rate == 48000.0
    assert first.size_bytes == (tmp_path / "a.nam").stat().st_size


def test_scan_models_missing_directory_returns_empty(tmp_path: Path) -> None:
    assert scan_models(tmp_path / "nope") == []


def test_scan_models_defaults_to_models_dir(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    (tmp_path / "solo.nam").write_text(_nam(), encoding="utf-8")
    monkeypatch.setenv("GITAR_MODELS_DIR", str(tmp_path))

    assert [model.name for model in scan_models()] == ["solo"]


def test_models_dir_honors_env_override(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITAR_MODELS_DIR", str(tmp_path))

    assert models_dir() == tmp_path


def test_models_dir_defaults_under_config_dir(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.delenv("GITAR_MODELS_DIR", raising=False)
    monkeypatch.setenv("GITAR_CONFIG_DIR", str(tmp_path))

    assert models_dir() == config_dir() / "models"
