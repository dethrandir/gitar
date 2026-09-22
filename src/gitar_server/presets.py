"""Rig presets: capture and restore the engine parameters as JSON files.

A preset stores the model, cabinet and tone controls that define a rig. Files
live as ``<name>.json`` under :func:`presets_dir` and are written atomically so
a crash mid-save cannot corrupt an existing preset.
"""

from __future__ import annotations

import json
import os
import re
import tempfile
from pathlib import Path

from pydantic import BaseModel, ValidationError

from gitar_server.config import config_dir

_CONTROL_CHARS = re.compile(r"[\x00-\x1f\x7f]")
_UNSAFE_FRAGMENTS = ("/", "\\", "..")


class PresetError(Exception):
    """Raised when a preset file cannot be read or validated."""


class PresetNotFoundError(PresetError):
    """Raised when the requested preset file does not exist."""


class Preset(BaseModel):
    name: str
    model_path: str = ""
    cab_ir_path: str = ""
    gain: float = 1.0
    gate_enabled: bool = True
    gate_threshold_db: float = -60.0
    eq_low_db: float = 0.0
    eq_mid_db: float = 0.0
    eq_high_db: float = 0.0


def presets_dir() -> Path:
    override = os.environ.get("GITAR_PRESETS_DIR")
    if override:
        return Path(override)
    return config_dir() / "presets"


def safe_name(name: str) -> str:
    trimmed = name.strip()
    if any(fragment in trimmed for fragment in _UNSAFE_FRAGMENTS) or _CONTROL_CHARS.search(trimmed):
        raise ValueError(f"invalid preset name: {name!r}")
    if not trimmed:
        raise ValueError("preset name must not be empty")
    return trimmed


def _preset_path(name: str) -> Path:
    return presets_dir() / f"{safe_name(name)}.json"


def list_presets() -> list[str]:
    root = presets_dir()
    if not root.is_dir():
        return []
    return sorted(path.stem for path in root.glob("*.json"))


def load_preset(name: str) -> Preset:
    safe = safe_name(name)
    path = presets_dir() / f"{safe}.json"
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise PresetNotFoundError(f"preset {safe!r} not found") from exc
    except OSError as exc:
        raise PresetError(f"cannot read preset {safe!r}: {exc}") from exc
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        raise PresetError(f"invalid preset {safe!r}: {exc}") from exc
    try:
        return Preset.model_validate(data)
    except ValidationError as exc:
        raise PresetError(f"invalid preset {safe!r}: {exc}") from exc


def save_preset(preset: Preset) -> Path:
    safe = safe_name(preset.name)
    directory = presets_dir()
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"{safe}.json"
    stored = preset.model_copy(update={"name": safe})
    payload = json.dumps(stored.model_dump(), indent=2) + "\n"
    handle, temp_name = tempfile.mkstemp(dir=directory, prefix=".preset-", suffix=".tmp")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            stream.write(payload)
        os.replace(temp_name, path)
    except BaseException:
        Path(temp_name).unlink(missing_ok=True)
        raise
    return path


def delete_preset(name: str) -> bool:
    path = _preset_path(name)
    try:
        path.unlink()
    except FileNotFoundError:
        return False
    except OSError as exc:
        raise PresetError(f"cannot delete preset {name!r}: {exc}") from exc
    return True
