"""Registry of neural amp model (``.nam``) files.

A ``.nam`` file is JSON with a top-level ``architecture`` string, a
``sample_rate`` number and an optional ``metadata`` object; the UI selects files
by their stem. Scanning tolerates unreadable or malformed files by skipping
them so one bad download cannot hide the rest of the library.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import cast

from gitar_server.config import config_dir


@dataclass(frozen=True)
class ModelInfo:
    name: str
    path: Path
    architecture: str
    sample_rate: float
    size_bytes: int


def models_dir() -> Path:
    override = os.environ.get("GITAR_MODELS_DIR")
    if override:
        return Path(override)
    return config_dir() / "models"


def parse_nam_metadata(text: str) -> dict[str, object]:
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid .nam JSON: {exc}") from exc
    if not isinstance(data, dict):
        raise ValueError("invalid .nam JSON: top-level value must be an object")

    architecture = data.get("architecture")
    if not isinstance(architecture, str):
        architecture = ""

    raw_rate = data.get("sample_rate")
    if isinstance(raw_rate, bool) or not isinstance(raw_rate, (int, float)):
        sample_rate = -1.0
    else:
        sample_rate = float(raw_rate)

    name = ""
    metadata = data.get("metadata")
    if isinstance(metadata, dict):
        raw_name = metadata.get("name")
        if isinstance(raw_name, str):
            name = raw_name

    return {"architecture": architecture, "sample_rate": sample_rate, "name": name}


def scan_models(directory: Path | None = None) -> list[ModelInfo]:
    root = directory if directory is not None else models_dir()
    if not root.is_dir():
        return []

    found: list[ModelInfo] = []
    for entry in sorted(root.glob("*.nam"), key=lambda path: path.name):
        try:
            metadata = parse_nam_metadata(entry.read_text(encoding="utf-8"))
            size_bytes = entry.stat().st_size
        except (OSError, ValueError):
            continue
        found.append(
            ModelInfo(
                name=entry.stem,
                path=entry,
                architecture=cast(str, metadata["architecture"]),
                sample_rate=cast(float, metadata["sample_rate"]),
                size_bytes=size_bytes,
            )
        )
    return found
