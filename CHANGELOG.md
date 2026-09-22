# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
  level meter, and a neural-amp engine panel with a model browser.
- **Model registry**: `.nam` scanning with metadata.
- English project documentation: README, architecture, troubleshooting,
  contributing, and this changelog.
- Repository hygiene: `.gitignore`, `.editorconfig`, `.gitattributes`,
  `.clang-format`, and `AGENTS.md`.
- CI: shellcheck, ruff, mypy, pytest (Linux + Windows), and the engine build.

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

[Unreleased]: https://github.com/dethrandir/gitar/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/dethrandir/gitar/releases/tag/v1.0.0
