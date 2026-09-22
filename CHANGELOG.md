# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Python control server skeleton (`gitar_server`, `gitard`) with `ruff`, `mypy`,
  and `pytest` configuration.
- English project documentation: README, architecture, troubleshooting,
  contributing, and this changelog.
- Repository hygiene: `.gitignore`, `.editorconfig`, `.gitattributes`, and
  `AGENTS.md`.

### Changed

- Documentation translated and standardized to English.

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
