# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.0.0] - 2026-09-23

### Added

- **Native engine** (`gitar-engine`, C++20): cross-device capture→playback
  routing via miniaudio, a lock-free interleaved bridge, a real-time gain
  processor, a peak/RMS level meter, and a noise gate.
- **Neural amp models**: NeuralAmpModelerCore integration with a `NeuralModel`
  wrapper for `.nam` models and WAV impulse responses, loaded into the audio
  path and swappable at runtime.
- **JSON control protocol** over a localhost socket, plus a `gitar-engine`
  CLI (`devices`, `run`, `version`) and a Python `EngineClient`.
- **Python control server** (`gitar_server`, `gitard`): FastAPI JSON API and
  WebSocket telemetry, config with legacy migration, PipeWire device
  discovery, a PipeWire/Guitarix backend, a Windows WASAPI placeholder, and
  engine process control.
- **Local web UI**: setup wizard, direct/amp routing, volume, tone loading,
  level meter, and a neural-amp engine panel with a model browser, three-band
  EQ, cabinet IR, presets, tuner, and spectrum analyzer.
- **Practice tools** in the engine: recording to WAV and a metronome.
- **Model registry**: `.nam` scanning with metadata.
- **Rig presets**: save/load/delete/apply a full rig (model, cab, gain, gate, EQ).
- **Packaging**: tag-triggered release workflow (engine binaries for Linux and
  Windows, Python wheel), PyPI trusted publishing, PowerShell and Linux v2
  installers, and a version-sync guard + bump script.
- English project documentation: README, architecture, troubleshooting,
  contributing, and this changelog.
- Repository hygiene: `.gitignore`, `.editorconfig`, `.gitattributes`,
  `.clang-format`, and `AGENTS.md`.
- CI: shellcheck, ruff, mypy, pytest (Linux + Windows), and the engine build.
- **Linux v2 installer** (`scripts/install-v2.sh`): a no-root one-liner that
  downloads `gitar-engine` and the control-server wheel from a GitHub release,
  with `--version`, `--bin-dir`, `--no-python`, and `--no-engine` options.
- **Windows installers**: `scripts/install.ps1` downloads `gitar-engine.exe`
  and the wheel from a GitHub release, installs the engine into
  `%LOCALAPPDATA%\gitar\bin`, updates the user PATH, and installs the wheel with
  `python -m pip install --user`; `scripts/uninstall.ps1` reverses it.
- **Release workflow**: `.github/workflows/release.yml` builds the Linux and
  Windows engine binaries and the Python wheel on `v*` tags and attaches them
  to a GitHub release.
- CI parse check for the PowerShell installers.
- **Version sync guard** (`tests/test_version.py`): the Python package and
  engine versions must match, and `scripts/bump_version.py` bumps both at once
  (`major`/`minor`/`patch` or an explicit version).
- **PyPI publishing**: the release workflow can publish the wheel and sdist to
  PyPI via OIDC trusted publishing; it is opt-in behind the `PYPI_PUBLISH`
  repository variable.

### Changed

- Documentation translated and standardized to English.
- Engine bumped to C++20 (required by the NAM core).

## [1.0.0] - 2026-09-22

### Added

- `gitar` bash CLI: route a guitar from a PipeWire source to a headphone sink,
  optionally through Guitarix.
- Commands: `amp`, `direct`, `stop`, `status`, `volume`, `tone
  clean|crunch|army`, `meter`, `setup`, `update`, `version`, with Turkish
  aliases.
- `install.sh` multi-distribution installer (dnf, apt, pacman, zypper, xbps) and
  `uninstall.sh`.

[Unreleased]: https://github.com/dethrandir/gitar/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/dethrandir/gitar/releases/tag/v2.0.0
[1.0.0]: https://github.com/dethrandir/gitar/releases/tag/v1.0.0
