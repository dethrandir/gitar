"""Guard that the Python package and engine versions stay in sync."""

from __future__ import annotations

import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_VERSION_FILE = REPO_ROOT / "src" / "gitar_server" / "__init__.py"
ENGINE_VERSION_FILE = REPO_ROOT / "engine" / "include" / "gitar" / "version.hpp"

_SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+(?:\.(?:dev|post)\d+)?$")
_PYTHON_VERSION_RE = re.compile(r'__version__\s*=\s*"([^"]*)"')
_ENGINE_VERSION_RE = re.compile(r'kEngineVersion\s*=\s*"([^"]*)"')


def _read_version(path: Path, pattern: re.Pattern[str]) -> str:
    match = pattern.search(path.read_text(encoding="utf-8"))
    assert match is not None, f"version declaration not found in {path}"
    return match.group(1)


def test_python_and_engine_versions_match() -> None:
    python_version = _read_version(PYTHON_VERSION_FILE, _PYTHON_VERSION_RE)
    engine_version = _read_version(ENGINE_VERSION_FILE, _ENGINE_VERSION_RE)
    assert python_version == engine_version


def test_python_version_is_semver() -> None:
    version = _read_version(PYTHON_VERSION_FILE, _PYTHON_VERSION_RE)
    assert _SEMVER_RE.match(version), version


def test_engine_version_is_semver() -> None:
    version = _read_version(ENGINE_VERSION_FILE, _ENGINE_VERSION_RE)
    assert _SEMVER_RE.match(version), version
