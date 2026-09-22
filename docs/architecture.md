# Architecture

This document describes how `gitar` is built. It is a living document; see
[`ROADMAP.md`](../ROADMAP.md) for what is implemented versus planned.

## Principles

1. **The audio path is native.** Real-time sample processing lives in the C++
   engine. No Python, no web request, ever touches a sample.
2. **The control path is boring.** The Python server handles configuration,
   device discovery, presets, and telemetry. It must be possible to restart the
   server without interrupting audio.
3. **Nothing is permanent.** Routing and engine state are applied at runtime and
   never written into system-wide audio configuration.
4. **Everything is testable.** Pure logic (parsing, config, protocol) is unit
   tested. The real-time engine keeps its DSP units host-agnostic and testable
   without a sound card.

## Components

```
┌─────────────────────────────────────────────────────────────┐
│ src/gitar_server/web/       local control panel (static)     │
│   static HTML/CSS/JS, no build step                         │
└───────────────▲─────────────────────────────────────────────┘
                │ HTTP + WebSocket (localhost)
┌───────────────┴─────────────────────────────────────────────┐
│ src/gitar_server/          Python control server (gitard)    │
│   config · devices · backends · presets · telemetry          │
└───────┬──────────────────────────────────────▲──────────────┘
        │ control protocol (JSON, localhost)   │ telemetry
┌───────▼──────────────────────────────────────┴──────────────┐
│ engine/                    native real-time audio engine    │
│   device I/O · DSP chain · neural amp models (.nam)         │
└─────────────────────────────────────────────────────────────┘
```

### Native engine (`engine/`)

- C++20, built with CMake.
- [`miniaudio`](https://miniaud.io/) for cross-platform device I/O: WASAPI on
  Windows, ALSA / PulseAudio / JACK on Linux. A single header, no external
  dependencies.
- Neural inference from `.nam` models, reusing the upstream
  NeuralAmpModelerCore DSP rather than reimplementing WaveNet.
- Signal chain: `input → gain → noise gate → NAM amp → output`. A three-band EQ
  and a cabinet IR stage (the latter via a WAV impulse response loaded as a
  linear NAM model) are planned.
- Exposes a small JSON control protocol on a localhost socket to the Python
  server. Audio state changes are applied on the audio thread through lock-free
  parameter passing.
- On Linux the engine can additionally drive the classic PipeWire/Guitarix route
  during migration.

### Control server (`src/gitar_server/`)

- FastAPI + Uvicorn. Only binds to `127.0.0.1`.
- Modules:
  - `config.py` — platform-aware config with legacy bash migration.
  - `devices.py` — PipeWire device/port discovery (`pactl`, `pw-link`).
  - `levels.py` — WAV level analysis for the meter.
  - `backends/` — one interface (`AudioBackend`) with platform backends:
    `PipeWireBackend` (Linux, wraps `pw-link`/`pactl`/Guitarix, a port of the
    legacy bash logic), `WasapiBackend` (Windows placeholder), and
    `NullBackend` (tests/unsupported platforms).
  - `engine_client.py` — synchronous client for the engine control protocol.
  - `engine_controller.py` — spawns and drives the `gitar-engine` process.
  - `models.py` — `.nam` model registry.
  - `api/` — REST routes, WebSocket telemetry, and the engine router.
- Serves the static web UI from `src/gitar_server/web/`.
- Never links audio libraries.

### Web UI (`web/`)

- Vanilla HTML, CSS, and JavaScript. No bundler, no Node required at install
  time.
- Talks to the server over REST for actions and WebSocket for live telemetry
  (input level, CPU, buffer underruns).
- Screens: setup wizard, amp/pedal chain, model browser, tuner, meter.

## Configuration

- User config lives in `$XDG_CONFIG_HOME/gitar` on Linux and
  `%APPDATA%\gitar` on Windows.
- Legacy bash config (`GIRIS`, `KANAL`, `CIKIS`, `GECIKME`) is read and migrated
  to the v2 schema on first load.
- Presets and downloaded models live under the same directory
  (`presets/`, `models/`).

## Versioning

The project follows [Semantic Versioning](https://semver.org/). The native
engine, Python package, and web UI share one version number, read from a single
source (`src/gitar_server/__init__.py` for Python, a generated header for C++).
