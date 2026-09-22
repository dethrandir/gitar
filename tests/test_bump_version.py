"""Tests for the ``scripts/bump_version.py`` version bump helper."""

from __future__ import annotations

import importlib.util
from pathlib import Path
from types import ModuleType

import pytest

REPO_ROOT = Path(__file__).resolve().parents[1]


def _load_bump_version() -> ModuleType:
    # scripts/ is not an importable package, so load the file by path rather
    # than polluting sys.path with a top-level module name.
    path = REPO_ROOT / "scripts" / "bump_version.py"
    spec = importlib.util.spec_from_file_location("bump_version", path)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


bump_version = _load_bump_version()


@pytest.mark.parametrize(
    ("current", "part", "expected"),
    [
        ("1.2.3", "major", "2.0.0"),
        ("1.2.3", "minor", "1.3.0"),
        ("1.2.3", "patch", "1.2.4"),
        ("2.0.0.dev0", "patch", "2.0.1"),
        ("2.0.0.dev0", "major", "3.0.0"),
        ("1.2.3", "2.1.0", "2.1.0"),
        ("2.0.0.dev0", "3.0.0", "3.0.0"),
    ],
)
def test_bump_semver(current: str, part: str, expected: str) -> None:
    assert bump_version.bump_semver(current, part) == expected


@pytest.mark.parametrize("part", ["nope", "1.2", "1.2.3.4", ""])
def test_bump_semver_rejects_bad_part(part: str) -> None:
    with pytest.raises(ValueError, match="not a bump part or version"):
        bump_version.bump_semver("1.2.3", part)


def test_replace_version_python() -> None:
    text = '__version__ = "1.2.3"\n'
    assert bump_version.replace_version(text, "2.0.0") == '__version__ = "2.0.0"\n'


def test_replace_version_engine() -> None:
    text = 'constexpr const char* kEngineVersion = "1.2.3";\n'
    assert (
        bump_version.replace_version(text, "2.0.0")
        == 'constexpr const char* kEngineVersion = "2.0.0";\n'
    )


def test_replace_version_without_declaration() -> None:
    with pytest.raises(ValueError, match="no version declaration found"):
        bump_version.replace_version("nothing to see here", "2.0.0")


def test_main_refuses_mismatched_versions(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    python_file = tmp_path / "__init__.py"
    engine_file = tmp_path / "version.hpp"
    python_file.write_text('__version__ = "1.2.3"\n', encoding="utf-8")
    engine_file.write_text('kEngineVersion = "1.2.4"\n', encoding="utf-8")
    monkeypatch.setattr(bump_version, "PYTHON_VERSION_FILE", python_file)
    monkeypatch.setattr(bump_version, "ENGINE_VERSION_FILE", engine_file)

    assert bump_version.main(["patch"]) == 1
    assert "disagree" in capsys.readouterr().err
    assert python_file.read_text(encoding="utf-8") == '__version__ = "1.2.3"\n'
    assert engine_file.read_text(encoding="utf-8") == 'kEngineVersion = "1.2.4"\n'


def test_main_updates_both_files(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    python_file = tmp_path / "__init__.py"
    engine_file = tmp_path / "version.hpp"
    python_file.write_text('__version__ = "1.2.3"\n', encoding="utf-8")
    engine_file.write_text('kEngineVersion = "1.2.3"\n', encoding="utf-8")
    monkeypatch.setattr(bump_version, "PYTHON_VERSION_FILE", python_file)
    monkeypatch.setattr(bump_version, "ENGINE_VERSION_FILE", engine_file)

    assert bump_version.main(["patch"]) == 0
    assert "1.2.3 -> 1.2.4" in capsys.readouterr().out
    assert python_file.read_text(encoding="utf-8") == '__version__ = "1.2.4"\n'
    assert engine_file.read_text(encoding="utf-8") == 'kEngineVersion = "1.2.4"\n'
