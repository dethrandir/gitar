"""Bump the gitar version across the Python package and the engine header."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
PYTHON_VERSION_FILE = REPO_ROOT / "src" / "gitar_server" / "__init__.py"
ENGINE_VERSION_FILE = REPO_ROOT / "engine" / "include" / "gitar" / "version.hpp"

_SEMVER_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:\.(?:dev|post)\d+)?$")
_VERSION_DECL_RE = re.compile(
    r'(?P<prefix>(?:__version__|kEngineVersion)\s*=\s*")(?P<version>[^"]*)(?P<suffix>")'
)
_BUMP_PARTS = ("major", "minor", "patch")


def extract_version(text: str) -> str:
    """Return the first version declaration found in ``text``."""
    match = _VERSION_DECL_RE.search(text)
    if match is None:
        raise ValueError("no version declaration found")
    return match.group("version")


def replace_version(text: str, new: str) -> str:
    """Replace the version declaration in ``text`` with ``new``."""
    if _VERSION_DECL_RE.search(text) is None:
        raise ValueError("no version declaration found")
    return _VERSION_DECL_RE.sub(
        lambda match: match.group("prefix") + new + match.group("suffix"), text
    )


def bump_semver(current: str, part: str) -> str:
    """Return the version produced by bumping ``current`` by ``part``.

    ``part`` is one of ``major``, ``minor``, ``patch`` or an explicit version.
    """
    if part not in _BUMP_PARTS:
        if _SEMVER_RE.match(part):
            return part
        raise ValueError(f"not a bump part or version: {part!r}")

    match = _SEMVER_RE.match(current)
    if match is None:
        raise ValueError(f"not a semver version: {current!r}")

    major, minor, patch = (int(match.group(index)) for index in (1, 2, 3))
    if part == "major":
        major, minor, patch = major + 1, 0, 0
    elif part == "minor":
        minor, patch = minor + 1, 0
    else:
        patch += 1
    return f"{major}.{minor}.{patch}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("part", help="major, minor, patch, or an explicit version like 2.1.0")
    args = parser.parse_args(argv)

    python_text = PYTHON_VERSION_FILE.read_text(encoding="utf-8")
    engine_text = ENGINE_VERSION_FILE.read_text(encoding="utf-8")
    python_version = extract_version(python_text)
    engine_version = extract_version(engine_text)

    if python_version != engine_version:
        print(
            f"error: versions already disagree "
            f"(python={python_version}, engine={engine_version}); fix them first",
            file=sys.stderr,
        )
        return 1

    try:
        new_version = bump_semver(python_version, args.part)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    PYTHON_VERSION_FILE.write_text(replace_version(python_text, new_version), encoding="utf-8")
    ENGINE_VERSION_FILE.write_text(replace_version(engine_text, new_version), encoding="utf-8")

    print(f"{python_version} -> {new_version}")
    print("Remember to add a CHANGELOG.md entry for the new version.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
