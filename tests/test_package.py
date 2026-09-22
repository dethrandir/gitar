"""Smoke tests for the gitar_server package metadata and CLI."""

import re

import gitar_server
from gitar_server.cli import main


def test_version_is_non_empty_semver_prefix() -> None:
    version = gitar_server.__version__
    assert isinstance(version, str)
    assert version
    assert re.match(r"\d+\.\d+", version)


def test_main_returns_zero() -> None:
    assert main(["version"]) == 0
