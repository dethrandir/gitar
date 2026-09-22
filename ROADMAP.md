# gitar — roadmap

Living plan. Checkboxes are checked **only after the manager has verified the
work by running the gate commands**. See `AGENTS.md` for conventions.

## Vision

`gitar` becomes a cross-platform, open-source guitar rig:

- **native real-time audio engine** (Windows + Linux) hosting **neural amp
  models** (`.nam`),
- **local web UI** driven by a **Python control server** (settings/control
  only — never the audio path),
- English by default, installable on Linux and Windows,
- reproducible builds, tests, and CI.

## Architecture

```
gitar/
├── engine/          C++17 real-time audio engine (miniaudio + NAM core)
├── src/gitar_server Python control server (FastAPI) + CLI (gitard)
├── web/             Local web UI (served by gitar_server, no build step)
├── scripts/         Platform installers (install.sh / install.ps1, uninstall)
├── docs/            Architecture, roadmap, troubleshooting (English)
├── tests/           Python tests
├── install.sh       Backward-compatible root entrypoint (existing users)
├── uninstall.sh     Backward-compatible root entrypoint
├── gitar            Legacy bash CLI (kept working during migration)
└── pyproject.toml   Python packaging (src-layout, ruff + mypy + pytest)
```

### Design decisions

- **Audio path is native, control path is Python.** Python never processes
  samples. On Linux the engine can also drive the existing PipeWire/Guitarix
  route; on Windows it uses WASAPI via miniaudio.
- **Engine binary name:** `gitar-engine`. Python package: `gitar_server`.
  Console scripts: `gitard` (server). Legacy bash CLI stays `gitar` at repo
  root so the published one-liner installer never breaks.
- **`.nam` support** reuses the upstream NeuralAmpModelerCore DSP (no
  reimplementing WaveNet inference).
- **Web UI ships as static assets** (vanilla JS + CSS) — no Node build step
  required for users.
- **Every phase ends green:** `ruff`, `mypy`, `pytest`, `shellcheck`,
  `clang-format --dry-run`, `cmake --build`. Locked in CI for Linux + Windows.

## Phases

### M0 — Repository foundation
- [x] M0.1 English docs skeleton: README, CONTRIBUTING, CHANGELOG, docs/*
- [x] M0.2 Repo hygiene: .gitignore, .editorconfig, .gitattributes, LICENSE check
- [x] M0.3 Python packaging skeleton (pyproject, src-layout, ruff/mypy/pytest)
- [x] M0.4 CI: shellcheck, ruff, mypy, pytest, cmake (conditional on engine/)
- [x] M0.5 C++ tooling config (.clang-format)

### M1 — Control server + web UI + Linux PipeWire backend
- [ ] M1.1 Config module (XDG + Windows paths), TDD
- [ ] M1.2 pactl/PipeWire parsing + device discovery, TDD
- [ ] M1.3 Backend abstraction + PipeWireBackend (port of bash logic), TDD
- [ ] M1.4 FastAPI app: REST + WebSocket telemetry, TDD
- [ ] M1.5 Web UI MVP (devices, connect/disconnect, volume, level meter)
- [ ] M1.6 `gitard` CLI + installer integration
- [ ] M1.7 Windows WASABIBackend stub + detection

### M2 — Native engine skeleton
- [ ] M2.1 CMake project + vendored miniaudio + passthrough, unit tests
- [ ] M2.2 Device I/O, latency config, lock-free ring buffer, level meter
- [ ] M2.3 JSON control protocol over localhost + Python engine client, TDD
- [ ] M2.4 Engine CLI (list devices, run, latency)

### M3 — Neural amp models
- [ ] M3.1 Integrate NAM DSP core, `.nam` loader, unit tests
- [ ] M3.2 DSP chain: gate → NAM amp → EQ → cab IR → output
- [ ] M3.3 Model registry + preset format (server side), TDD
- [ ] M3.4 Web UI: model browser, chain editor, tuner, spectrum

### M4 — Web UI polish
- [ ] M4.1 Knobs/faders component, responsive layout, dark theme
- [ ] M4.2 Recording + metronome + tuner
- [ ] M4.3 Preset save/load/share

### M5 — Windows
- [ ] M5.1 WASAPI shared/exclusive device enumeration + selection
- [ ] M5.2 PowerShell installer/uninstaller, PATH + start menu
- [ ] M5.3 Windows CI build + release artifact
- [ ] M5.4 Manual test checklist on Windows

### M6 — Packaging & release
- [ ] M6.1 Build native binaries (Linux + Windows) and attach to releases
- [ ] M6.2 `pip install gitar` wheel publishing workflow
- [ ] M6.3 Versioning + changelog automation
- [ ] M6.4 One-line installers download matching engine binary

## Open questions / decisions log

- (2026-09-22) Engine stack: `miniaudio` (single-header, WASAPI/ALSA/PulseAudio/JACK/CoreAudio)
  + NeuralAmpModelerCore. Chosen for zero-dependency builds on both targets.
- (2026-09-22) Python package name `gitar_server`; console script `gitard`.
  Avoids collision with the root-level bash `gitar`.
- (2026-09-22) Root `install.sh` / `uninstall.sh` / `gitar` stay in place for
  backward compatibility with the published one-liner.
